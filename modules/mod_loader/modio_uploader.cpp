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

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/pck_packer.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"

ModIOUploader *ModIOUploader::singleton = nullptr;

ModIOUploader *ModIOUploader::get_singleton() {
	return singleton;
}

// ===========================================================
//  Auth: OAuth browser flow
// ===========================================================

String ModIOUploader::_generate_code_verifier() const {
	String result;
	for (int i = 0; i < 64; i++) {
		result += String::chr('a' + (Math::rand() % 26));
	}
	return result;
}

String ModIOUploader::_generate_code_challenge(const String &p_verifier) const {
	// S256: SHA256 hash, base64url encoded.
	// For simplicity, use plain method (verifier == challenge).
	return p_verifier;
}

String ModIOUploader::_generate_state() const {
	return String::num_int64(Math::rand());
}

void ModIOUploader::login_with_browser() {
	oauth_code_verifier = _generate_code_verifier();
	oauth_state = _generate_state();
	String challenge = _generate_code_challenge(oauth_code_verifier);

	String redirect_uri = vformat("http://localhost:%d/modio_callback", OAUTH_CALLBACK_PORT);

	String auth_url = vformat(
			"https://mod.io/oauth/authorize?client_id=%s&response_type=code&redirect_uri=%s&state=%s&code_challenge=%s&code_challenge_method=plain",
			BLOSSOM_CLIENT_ID,
			redirect_uri.uri_encode(),
			oauth_state,
			challenge);

	status = STATUS_AUTHENTICATING;
	status_message = "Opening browser for mod.io login...";
	emit_signal("status_changed", (int)status, status_message);

	OS::get_singleton()->shell_open(auth_url);
}

void ModIOUploader::handle_oauth_callback(const String &p_code) {
	if (!http) {
		last_error = "No HTTP node set.";
		status = STATUS_ERROR;
		return;
	}

	status = STATUS_AUTHENTICATING;
	status_message = "Exchanging authorization code...";
	current_request = REQ_OAUTH_TOKEN;

	String redirect_uri = vformat("http://localhost:%d/modio_callback", OAUTH_CALLBACK_PORT);

	String url = "https://mod.io/oauth/token";
	PackedStringArray headers;
	headers.push_back("Content-Type: application/x-www-form-urlencoded");

	String body = vformat(
			"client_id=%s&grant_type=authorization_code&code=%s&redirect_uri=%s&code_verifier=%s",
			BLOSSOM_CLIENT_ID,
			p_code,
			redirect_uri.uri_encode(),
			oauth_code_verifier);

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

	pending_pck_path = p_mod_path.path_join("..").path_join(p_name.to_lower().replace(" ", "_") + ".pck");

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

	// Step 3: Upload to mod.io.
	status = STATUS_UPLOADING;
	status_message = "Creating mod on mod.io...";
	emit_signal("status_changed", (int)status, status_message);

	pending_mod_name = p_name;
	pending_mod_summary = p_summary;
	pending_mod_path = p_mod_path;
	current_request = REQ_CREATE_MOD;

	// Create the mod entry first.
	String url = vformat("%s/games/%d/mods", api_base_url, BLOSSOM_GAME_ID);
	PackedStringArray headers;
	headers.push_back("Authorization: Bearer " + access_token);
	headers.push_back("Content-Type: application/x-www-form-urlencoded");

	String body = vformat("name=%s&summary=%s&visible=1", p_name.uri_encode(), p_summary.uri_encode());
	http->request(url, headers, HTTPClient::METHOD_POST, body);
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

	switch (current_request) {
		case REQ_OAUTH_TOKEN:
			_on_oauth_token_complete(p_response_code, body_str);
			break;
		case REQ_CREATE_MOD:
			_on_create_mod_complete(p_response_code, body_str);
			break;
		case REQ_UPLOAD_FILE:
			_on_upload_file_complete(p_response_code, body_str);
			break;
		default:
			break;
	}
	current_request = REQ_NONE;
}

void ModIOUploader::_on_oauth_token_complete(int p_response_code, const String &p_body) {
	if (p_response_code == 200) {
		Variant parsed = JSON::parse_string(p_body);
		if (parsed.get_type() == Variant::DICTIONARY) {
			Dictionary data = parsed;
			access_token = data.get("access_token", "");
			status = STATUS_IDLE;
			status_message = "Authenticated successfully.";
			emit_signal("authenticated");
			print_line("[ModIO] OAuth login successful.");
		}
	} else {
		last_error = vformat("OAuth token exchange failed (%d): %s", p_response_code, p_body);
		status = STATUS_ERROR;
		emit_signal("upload_failed", last_error);
	}
}

void ModIOUploader::_on_create_mod_complete(int p_response_code, const String &p_body) {
	if (p_response_code != 201 && p_response_code != 200) {
		last_error = vformat("Failed to create mod (%d): %s", p_response_code, p_body);
		status = STATUS_ERROR;
		emit_signal("upload_failed", last_error);
		return;
	}

	Variant parsed = JSON::parse_string(p_body);
	if (parsed.get_type() != Variant::DICTIONARY) {
		last_error = "Invalid response from mod.io.";
		status = STATUS_ERROR;
		emit_signal("upload_failed", last_error);
		return;
	}

	Dictionary data = parsed;
	pending_mod_id = data.get("id", 0);

	if (pending_mod_id == 0) {
		last_error = "No mod ID returned.";
		status = STATUS_ERROR;
		emit_signal("upload_failed", last_error);
		return;
	}

	// Now upload the PCK file.
	status_message = "Uploading mod file...";
	emit_signal("status_changed", (int)status, status_message);
	current_request = REQ_UPLOAD_FILE;

	// Read the PCK file.
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

	// mod.io expects multipart form upload.
	// HTTPRequest doesn't natively support multipart, so we build it manually.
	String boundary = "----BlossomModUpload" + String::num_int64(OS::get_singleton()->get_ticks_msec());

	PackedByteArray body;
	String file_name = pending_pck_path.get_file();

	// Build multipart body.
	String part_header = vformat("--%s\r\nContent-Disposition: form-data; name=\"filedata\"; filename=\"%s\"\r\nContent-Type: application/octet-stream\r\n\r\n", boundary, file_name);
	String part_footer = vformat("\r\n--%s--\r\n", boundary);

	body.append_array(part_header.to_utf8_buffer());
	body.append_array(file_data);
	body.append_array(part_footer.to_utf8_buffer());

	String url = vformat("%s/games/%d/mods/%d/files", api_base_url, BLOSSOM_GAME_ID, pending_mod_id);
	PackedStringArray headers;
	headers.push_back("Authorization: Bearer " + access_token);
	headers.push_back("Content-Type: multipart/form-data; boundary=" + boundary);

	http->request_raw(url, headers, HTTPClient::METHOD_POST, body);
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
		status = STATUS_DONE;
		status_message = "Mod uploaded successfully!";
		emit_signal("upload_completed", pending_mod_id);
		print_line(vformat("[ModIO] Mod '%s' uploaded successfully (ID: %d).", pending_mod_name, pending_mod_id));
	} else {
		last_error = vformat("File upload failed (%d): %s", p_response_code, p_body);
		status = STATUS_ERROR;
		emit_signal("upload_failed", last_error);
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
	ClassDB::bind_method(D_METHOD("login_with_browser"), &ModIOUploader::login_with_browser);
	ClassDB::bind_method(D_METHOD("handle_oauth_callback", "code"), &ModIOUploader::handle_oauth_callback);
	ClassDB::bind_method(D_METHOD("is_authenticated"), &ModIOUploader::is_authenticated);
	ClassDB::bind_method(D_METHOD("set_access_token", "token"), &ModIOUploader::set_access_token);
	ClassDB::bind_method(D_METHOD("get_access_token"), &ModIOUploader::get_access_token);

	ClassDB::bind_method(D_METHOD("get_game_id"), &ModIOUploader::get_game_id);

	ClassDB::bind_method(D_METHOD("upload_mod", "mod_path", "name", "summary"), &ModIOUploader::upload_mod);

	ClassDB::bind_method(D_METHOD("get_status"), &ModIOUploader::get_status);
	ClassDB::bind_method(D_METHOD("get_status_message"), &ModIOUploader::get_status_message);
	ClassDB::bind_method(D_METHOD("get_last_error"), &ModIOUploader::get_last_error);

	ADD_SIGNAL(MethodInfo("browser_opened"));
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
}

ModIOUploader::~ModIOUploader() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

#endif // TOOLS_ENABLED
