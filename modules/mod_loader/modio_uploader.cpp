/**************************************************************************/
/*  modio_uploader.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#ifdef TOOLS_ENABLED

#include "modio_uploader.h"
#include "mod_validator.h"

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/pck_packer.h"
#include "modules/zip/zip_packer.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"

ModIOUploader *ModIOUploader::singleton = nullptr;

ModIOUploader *ModIOUploader::get_singleton() {
	return singleton;
}

// ===========================================================
//  Auth: email-based
// ===========================================================

void ModIOUploader::_save_token() {
	String path = OS::get_singleton()->get_user_data_dir().path_join(".modio_token");
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
	if (f.is_valid()) {
		f->store_string(access_token);
	}
}

void ModIOUploader::_load_token() {
	String path = OS::get_singleton()->get_user_data_dir().path_join(".modio_token");
	if (FileAccess::exists(path)) {
		Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
		if (f.is_valid()) {
			access_token = f->get_as_text().strip_edges();
		}
	}
}

bool ModIOUploader::has_api_key() const {
	return true; // Hardcoded.
}

void ModIOUploader::request_email_code(const String &p_email) {
	if (!http) {
		last_error = "No HTTP node set.";
		status = STATUS_ERROR;
		return;
	}

	status = STATUS_AUTHENTICATING;
	status_message = "Sending security code to " + p_email + "...";
	current_request = REQ_EMAIL_REQUEST;

	String url = vformat("%s/oauth/emailrequest?api_key=%s", api_base_url, BLOSSOM_API_KEY);
	PackedStringArray headers;
	headers.push_back("Content-Type: application/x-www-form-urlencoded");

	String body = "email=" + p_email.uri_encode();
	http->request(url, headers, HTTPClient::METHOD_POST, body);
}

void ModIOUploader::exchange_email_code(const String &p_security_code) {
	if (!http) {
		last_error = "No HTTP node set.";
		status = STATUS_ERROR;
		return;
	}

	status = STATUS_AUTHENTICATING;
	status_message = "Verifying security code...";
	current_request = REQ_EMAIL_EXCHANGE;

	String url = vformat("%s/oauth/emailexchange?api_key=%s", api_base_url, BLOSSOM_API_KEY);
	PackedStringArray headers;
	headers.push_back("Content-Type: application/x-www-form-urlencoded");

	String body = "security_code=" + p_security_code;
	http->request(url, headers, HTTPClient::METHOD_POST, body);
}

bool ModIOUploader::is_authenticated() const {
	return !access_token.is_empty();
}

void ModIOUploader::set_access_token(const String &p_token) {
	access_token = p_token;
}

String ModIOUploader::get_access_token() const {
	return access_token;
}

// ===========================================================
//  Upload flow
// ===========================================================

void ModIOUploader::upload_mod(const String &p_mod_path, const String &p_name, const String &p_summary) {
	if (!is_authenticated()) {
		last_error = "Not authenticated. Call request_email_code first.";
		status = STATUS_ERROR;
		emit_signal("upload_failed", last_error);
		return;
	}

	// Step 1: Validate.
	status = STATUS_VALIDATING;
	status_message = "Validating mod...";
	emit_signal("status_changed", (int)status, status_message);

	ModValidator *validator = ModValidator::get_singleton();
	if (validator) {
		Dictionary result = validator->validate_mod(p_mod_path);
		if (!bool(result["passed"])) {
			PackedStringArray errors = result["errors"];
			last_error = "Validation failed:\n" + String("\n").join(errors);
			status = STATUS_ERROR;
			emit_signal("upload_failed", last_error);
			return;
		}
	}

	// Step 2: Package as PCK.
	status = STATUS_PACKAGING;
	status_message = "Packaging mod...";
	emit_signal("status_changed", (int)status, status_message);

	pending_pck_path = OS::get_singleton()->get_user_data_dir().path_join(p_name.to_lower().replace(" ", "_") + ".pck");

	Ref<PCKPacker> packer;
	packer.instantiate();
	Error err = packer->pck_start(pending_pck_path);
	if (err != OK) {
		last_error = "Failed to create PCK file.";
		status = STATUS_ERROR;
		emit_signal("upload_failed", last_error);
		return;
	}

	// Add all files from the mod directory.
	Vector<String> dirs_to_scan;
	dirs_to_scan.push_back(p_mod_path);

	while (!dirs_to_scan.is_empty()) {
		String dir = dirs_to_scan[dirs_to_scan.size() - 1];
		dirs_to_scan.remove_at(dirs_to_scan.size() - 1);

		Ref<DirAccess> da = DirAccess::open(dir);
		if (da.is_null()) {
			continue;
		}
		da->list_dir_begin();
		String entry = da->get_next();
		while (!entry.is_empty()) {
			if (entry != "." && entry != "..") {
				String full = dir.path_join(entry);
				if (da->current_is_dir()) {
					dirs_to_scan.push_back(full);
				} else {
					// Map to res:// path.
					String rel = full.replace(p_mod_path, "res://");
					packer->add_file(rel, full);
				}
			}
			entry = da->get_next();
		}
		da->list_dir_end();
	}

	packer->flush();

	// Step 2b: Wrap the PCK in a zip (mod.io requires zip uploads).
	String zip_path = pending_pck_path.replace(".pck", ".zip");
	{
		Ref<ZIPPacker> zipper;
		zipper.instantiate();
		Error zerr = zipper->open(zip_path, ZIPPacker::APPEND_CREATE);
		if (zerr != OK) {
			last_error = "Failed to create zip file.";
			status = STATUS_ERROR;
			emit_signal("upload_failed", last_error);
			return;
		}

		// Read the PCK file and add it to the zip.
		Ref<FileAccess> pck_file = FileAccess::open(pending_pck_path, FileAccess::READ);
		if (pck_file.is_valid()) {
			uint64_t pck_size = pck_file->get_length();
			Vector<uint8_t> pck_data;
			pck_data.resize(pck_size);
			pck_file->get_buffer(pck_data.ptrw(), pck_size);
			pck_file.unref();

			zipper->start_file(pending_pck_path.get_file());
			zipper->write_file(pck_data);
			zipper->close_file();
		}
		zipper->close();

		// Delete the standalone PCK, keep only the zip.
		Ref<DirAccess> cleanup_da = DirAccess::open(pending_pck_path.get_base_dir());
		if (cleanup_da.is_valid()) {
			cleanup_da->remove(pending_pck_path.get_file());
		}
	}
	pending_pck_path = zip_path;

	// Step 3: Read mod.io ID from mod.cfg.
	String cfg_path = p_mod_path.path_join("mod.cfg");
	Ref<ConfigFile> cfg;
	cfg.instantiate();
	int modio_id = 0;
	if (cfg->load(cfg_path) == OK) {
		modio_id = cfg->get_value("mod", "modio_id", 0);
	}

	if (modio_id == 0) {
		last_error = "No modio_id found in mod.cfg. Create the UGC project first.";
		status = STATUS_ERROR;
		emit_signal("upload_failed", last_error);
		return;
	}

	pending_mod_id = modio_id;
	pending_mod_name = p_name;
	pending_mod_path = p_mod_path;

	// Step 4: Upload the PCK file directly to the existing mod.
	status = STATUS_UPLOADING;
	status_message = "Uploading UGC file...";
	emit_signal("status_changed", (int)status, status_message);

	current_request = REQ_UPLOAD_FILE;

	Ref<FileAccess> f = FileAccess::open(pending_pck_path, FileAccess::READ);
	if (f.is_null()) {
		last_error = "Failed to read PCK file for upload.";
		status = STATUS_ERROR;
		emit_signal("upload_failed", last_error);
		return;
	}

	uint64_t file_size = f->get_length();
	PackedByteArray file_data;
	file_data.resize(file_size);
	f->get_buffer(file_data.ptrw(), file_size);
	f.unref();

	String boundary = "----BlossomUpload" + String::num_int64(OS::get_singleton()->get_ticks_msec());
	String file_name = pending_pck_path.get_file();

	// Read version from mod.cfg.
	String mod_version = "1.0.0";
	Ref<ConfigFile> ver_cfg;
	ver_cfg.instantiate();
	if (ver_cfg->load(p_mod_path.path_join("mod.cfg")) == OK) {
		mod_version = ver_cfg->get_value("mod", "version", "1.0.0");
	}

	PackedByteArray body;
	auto add_text_field = [&](const String &p_fname, const String &p_value) {
		String part = vformat("--%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n\r\n%s\r\n", boundary, p_fname, p_value);
		body.append_array(part.to_utf8_buffer());
	};

	add_text_field("version", mod_version);
	add_text_field("changelog", "Uploaded from Blossom Engine");
	add_text_field("active", "true");

	// Add the zip file.
	String part_header = vformat("--%s\r\nContent-Disposition: form-data; name=\"filedata\"; filename=\"%s\"\r\nContent-Type: application/zip\r\n\r\n", boundary, file_name);
	body.append_array(part_header.to_utf8_buffer());
	body.append_array(file_data);
	body.append_array(String("\r\n").to_utf8_buffer());

	String part_footer = vformat("--%s--\r\n", boundary);
	body.append_array(part_footer.to_utf8_buffer());

	String url = vformat("%s/games/%d/mods/%d/files", api_base_url, BLOSSOM_GAME_ID, pending_mod_id);
	PackedStringArray headers;
	headers.push_back("Authorization: Bearer " + access_token);
	headers.push_back("Content-Type: multipart/form-data; boundary=" + boundary);

	print_line(vformat("[ModIO] Uploading PCK (%d bytes) to mod %d", file_data.size(), pending_mod_id));
	http->request_raw(url, headers, HTTPClient::METHOD_POST, body);
}

// ===========================================================
//  HTTP callbacks
// ===========================================================

void ModIOUploader::_http_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	String body_str;
	if (!p_body.is_empty()) {
		const uint8_t *r = p_body.ptr();
		body_str = String::utf8((const char *)r, p_body.size());
	}

	print_line(vformat("[ModIO] HTTP response (req=%d, code=%d): %s", (int)current_request, p_response_code, body_str));

	RequestType req = current_request;
	current_request = REQ_NONE;

	switch (req) {
		case REQ_EMAIL_REQUEST:
			_on_email_request_complete(p_response_code, body_str);
			break;
		case REQ_EMAIL_EXCHANGE:
			_on_email_exchange_complete(p_response_code, body_str);
			break;
		case REQ_UPLOAD_FILE:
			_on_upload_file_complete(p_response_code, body_str);
			break;
		case REQ_SET_VISIBLE:
			_on_set_visible_complete(p_response_code, body_str);
			break;
		default:
			print_line(vformat("[ModIO] WARNING: Unhandled response for request type %d", (int)req));
			break;
	}
}

void ModIOUploader::_on_email_request_complete(int p_response_code, const String &p_body) {
	if (p_response_code == 200) {
		status_message = "Security code sent! Check your email.";
		emit_signal("status_changed", (int)status, status_message);
	} else {
		last_error = vformat("Email request failed (%d): %s", p_response_code, p_body);
		status = STATUS_ERROR;
		emit_signal("upload_failed", last_error);
	}
}

void ModIOUploader::_on_email_exchange_complete(int p_response_code, const String &p_body) {
	if (p_response_code == 200) {
		Variant parsed = JSON::parse_string(p_body);
		if (parsed.get_type() == Variant::DICTIONARY) {
			Dictionary data = parsed;
			access_token = data.get("access_token", "");
			_save_token();
			status = STATUS_IDLE;
			status_message = "Logged in successfully.";
			emit_signal("authenticated");
			print_line("[ModIO] Email auth successful.");
		}
	} else {
		last_error = vformat("Code exchange failed (%d): %s", p_response_code, p_body);
		status = STATUS_ERROR;
		emit_signal("upload_failed", last_error);
	}
}

void ModIOUploader::_on_upload_file_complete(int p_response_code, const String &p_body) {
	// Clean up temp PCK.
	if (FileAccess::exists(pending_pck_path)) {
		Ref<DirAccess> da = DirAccess::open(pending_pck_path.get_base_dir());
		if (da.is_valid()) {
			da->remove(pending_pck_path.get_file());
		}
	}

	if (p_response_code == 201 || p_response_code == 200) {
		// File uploaded — now make the mod visible.
		status_message = "Publishing UGC...";
		emit_signal("status_changed", (int)status, status_message);
		current_request = REQ_SET_VISIBLE;

		String url = vformat("%s/games/%d/mods/%d", api_base_url, BLOSSOM_GAME_ID, pending_mod_id);

		PackedStringArray vis_headers;
		vis_headers.push_back("Authorization: Bearer " + access_token);
		vis_headers.push_back("Content-Type: application/x-www-form-urlencoded");

		print_line(vformat("[ModIO] Setting visible=1: PUT %s", url));
		http->request(url, vis_headers, HTTPClient::METHOD_PUT, "visible=1");
		return;
	} else {
		last_error = vformat("File upload failed (%d): %s", p_response_code, p_body);
		status = STATUS_ERROR;
		emit_signal("upload_failed", last_error);
	}
}

void ModIOUploader::_on_set_visible_complete(int p_response_code, const String &p_body) {
	if (p_response_code == 200) {
		// Extract profile_url from response.
		String mod_url = "https://mod.io/g/vcvr/m/" + pending_mod_name.to_lower().replace(" ", "-");
		Variant parsed = JSON::parse_string(p_body);
		if (parsed.get_type() == Variant::DICTIONARY) {
			Dictionary data = parsed;
			mod_url = data.get("profile_url", mod_url);
		}

		status = STATUS_DONE;
		status_message = "UGC published! " + mod_url;
		emit_signal("upload_completed", pending_mod_id);
		print_line(vformat("[ModIO] UGC '%s' published: %s", pending_mod_name, mod_url));
		OS::get_singleton()->shell_open(mod_url);
	} else {
		// Upload succeeded but visibility failed — still report success.
		status = STATUS_DONE;
		status_message = "UGC uploaded (set visible manually on mod.io).";
		emit_signal("upload_completed", pending_mod_id);
		print_line(vformat("[ModIO] UGC uploaded but visibility update failed (%d): %s", p_response_code, p_body));
	}
}

// ===========================================================
//  Status
// ===========================================================

ModIOUploader::UploadStatus ModIOUploader::get_status() const {
	return status;
}

String ModIOUploader::get_status_message() const {
	return status_message;
}

String ModIOUploader::get_last_error() const {
	return last_error;
}

void ModIOUploader::set_http_node(HTTPRequest *p_http) {
	if (http && http->is_connected("request_completed", callable_mp(this, &ModIOUploader::_http_completed))) {
		http->disconnect("request_completed", callable_mp(this, &ModIOUploader::_http_completed));
	}
	http = p_http;
	if (http) {
		http->connect("request_completed", callable_mp(this, &ModIOUploader::_http_completed));
	}
}

// ===========================================================
//  Binding
// ===========================================================

void ModIOUploader::_bind_methods() {
	ClassDB::bind_method(D_METHOD("request_email_code", "email"), &ModIOUploader::request_email_code);
	ClassDB::bind_method(D_METHOD("exchange_email_code", "security_code"), &ModIOUploader::exchange_email_code);
	ClassDB::bind_method(D_METHOD("has_api_key"), &ModIOUploader::has_api_key);
	ClassDB::bind_method(D_METHOD("is_authenticated"), &ModIOUploader::is_authenticated);
	ClassDB::bind_method(D_METHOD("set_access_token", "token"), &ModIOUploader::set_access_token);
	ClassDB::bind_method(D_METHOD("get_access_token"), &ModIOUploader::get_access_token);

	ClassDB::bind_method(D_METHOD("get_game_id"), &ModIOUploader::get_game_id);

	ClassDB::bind_method(D_METHOD("upload_mod", "mod_path", "name", "summary"), &ModIOUploader::upload_mod);

	ClassDB::bind_method(D_METHOD("get_status"), &ModIOUploader::get_status);
	ClassDB::bind_method(D_METHOD("get_status_message"), &ModIOUploader::get_status_message);
	ClassDB::bind_method(D_METHOD("get_last_error"), &ModIOUploader::get_last_error);

	ADD_SIGNAL(MethodInfo("email_code_sent"));
	ADD_SIGNAL(MethodInfo("authenticated"));
	ADD_SIGNAL(MethodInfo("status_changed", PropertyInfo(Variant::INT, "status"), PropertyInfo(Variant::STRING, "message")));
	ADD_SIGNAL(MethodInfo("upload_completed", PropertyInfo(Variant::INT, "mod_id")));
	ADD_SIGNAL(MethodInfo("upload_failed", PropertyInfo(Variant::STRING, "error")));

	BIND_ENUM_CONSTANT(STATUS_IDLE);
	BIND_ENUM_CONSTANT(STATUS_AUTHENTICATING);
	BIND_ENUM_CONSTANT(STATUS_VALIDATING);
	BIND_ENUM_CONSTANT(STATUS_PACKAGING);
	BIND_ENUM_CONSTANT(STATUS_UPLOADING);
	BIND_ENUM_CONSTANT(STATUS_DONE);
	BIND_ENUM_CONSTANT(STATUS_ERROR);
}

ModIOUploader::ModIOUploader() {
	singleton = this;
	_load_token();
}

ModIOUploader::~ModIOUploader() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

#endif // TOOLS_ENABLED
