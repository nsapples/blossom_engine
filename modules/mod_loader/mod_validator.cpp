/**************************************************************************/
/*  mod_validator.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#include "mod_validator.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"

ModValidator *ModValidator::singleton = nullptr;

// Blocked method calls — these are dangerous in mods.
const char *ModValidator::BLOCKED_CALLS[] = {
	"OS.execute",
	"OS.shell_open",
	"OS.kill",
	"OS.create_process",
	"OS.create_instance",
	"OS.set_environment",
	"OS.unset_environment",
	"OS.crash",
	"OS.delay_msec",
	"OS.delay_usec",
	"JavaScriptBridge",
	"Thread.new",
	"Thread.start",
	"Mutex.new",
	"Semaphore.new",
	"GDExtensionManager",
	"GDExtension",
	"NativeExtension",
	"load_extension",
	"ClassDB.instantiate",
	"Engine.register_singleton",
	"ProjectSettings.save",
	"EditorInterface",
	"EditorPlugin",
	nullptr,
};

// Blocked class instantiations.
const char *ModValidator::BLOCKED_CLASSES[] = {
	"HTTPRequest",
	"HTTPClient",
	"StreamPeerTCP",
	"StreamPeerTLS",
	"PacketPeerUDP",
	"TCPServer",
	"UDPServer",
	"WebSocketPeer",
	"MultiplayerPeer",
	"ENetMultiplayerPeer",
	"WebRTCPeerConnection",
	"Thread",
	nullptr,
};

ModValidator *ModValidator::get_singleton() {
	return singleton;
}

void ModValidator::_scan_line(const String &p_line, int p_line_num, const String &p_file, ValidationResult &r_result) {
	String stripped = p_line.strip_edges();

	// Skip comments and empty lines.
	if (stripped.is_empty() || stripped.begins_with("#")) {
		return;
	}

	// Check blocked calls.
	for (int i = 0; BLOCKED_CALLS[i] != nullptr; i++) {
		if (stripped.find(BLOCKED_CALLS[i]) != -1) {
			r_result.passed = false;
			r_result.errors.push_back(vformat("%s:%d: Blocked call '%s' is not allowed in mods.", p_file, p_line_num, BLOCKED_CALLS[i]));
		}
	}

	// Check blocked class instantiations.
	for (int i = 0; BLOCKED_CLASSES[i] != nullptr; i++) {
		String pattern = vformat("%s.new", BLOCKED_CLASSES[i]);
		if (stripped.find(pattern) != -1) {
			r_result.passed = false;
			r_result.errors.push_back(vformat("%s:%d: Blocked class '%s' cannot be instantiated in mods.", p_file, p_line_num, BLOCKED_CLASSES[i]));
		}
	}

	// Check for filesystem access outside res://.
	if (stripped.find("FileAccess.open") != -1) {
		// Allow res:// only.
		if (stripped.find("\"user://") != -1 || stripped.find("\"C:") != -1 || stripped.find("\"/") != -1) {
			r_result.passed = false;
			r_result.errors.push_back(vformat("%s:%d: FileAccess outside res:// is not allowed in mods.", p_file, p_line_num));
		} else {
			r_result.warnings.push_back(vformat("%s:%d: FileAccess detected — ensure it only accesses res:// paths.", p_file, p_line_num));
		}
	}

	// Check for DirAccess.
	if (stripped.find("DirAccess") != -1) {
		r_result.warnings.push_back(vformat("%s:%d: DirAccess detected — mod filesystem access is restricted.", p_file, p_line_num));
	}

	// Check for preload/load of native extensions.
	if (stripped.find(".gdextension") != -1 || stripped.find(".so\"") != -1 || stripped.find(".dll\"") != -1 || stripped.find(".dylib\"") != -1) {
		r_result.passed = false;
		r_result.errors.push_back(vformat("%s:%d: Native code loading is not allowed in mods.", p_file, p_line_num));
	}
}

ModValidator::ValidationResult ModValidator::_validate_script(const String &p_path, const String &p_content) {
	ValidationResult result;

	PackedStringArray lines = p_content.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		_scan_line(lines[i], i + 1, p_path, result);
	}

	return result;
}

Dictionary ModValidator::validate_script_file(const String &p_path) {
	Dictionary dict;
	dict["passed"] = false;
	dict["errors"] = PackedStringArray();
	dict["warnings"] = PackedStringArray();

	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
	if (f.is_null()) {
		PackedStringArray errors;
		errors.push_back("Failed to open file: " + p_path);
		dict["errors"] = errors;
		return dict;
	}

	String content = f->get_as_text();
	ValidationResult result = _validate_script(p_path, content);

	dict["passed"] = result.passed;
	dict["errors"] = result.errors;
	dict["warnings"] = result.warnings;
	return dict;
}

Dictionary ModValidator::validate_mod(const String &p_mod_path) {
	Dictionary dict;
	dict["passed"] = true;
	PackedStringArray all_errors;
	PackedStringArray all_warnings;
	int scripts_checked = 0;

	// Recursively find all .gd files.
	Vector<String> dirs_to_scan;
	dirs_to_scan.push_back(p_mod_path);

	while (!dirs_to_scan.is_empty()) {
		String dir_path = dirs_to_scan[dirs_to_scan.size() - 1];
		dirs_to_scan.remove_at(dirs_to_scan.size() - 1);

		Ref<DirAccess> da = DirAccess::open(dir_path);
		if (da.is_null()) {
			continue;
		}

		da->list_dir_begin();
		String entry = da->get_next();
		while (!entry.is_empty()) {
			if (entry == "." || entry == "..") {
				entry = da->get_next();
				continue;
			}

			String full = dir_path.path_join(entry);

			if (da->current_is_dir()) {
				dirs_to_scan.push_back(full);
			} else if (entry.get_extension() == "gd") {
				Ref<FileAccess> f = FileAccess::open(full, FileAccess::READ);
				if (f.is_valid()) {
					ValidationResult result = _validate_script(full, f->get_as_text());
					if (!result.passed) {
						dict["passed"] = false;
					}
					all_errors.append_array(result.errors);
					all_warnings.append_array(result.warnings);
					scripts_checked++;
				}
			} else if (entry.get_extension() == "gdextension" || entry.get_extension() == "so" || entry.get_extension() == "dll" || entry.get_extension() == "dylib") {
				dict["passed"] = false;
				all_errors.push_back("Native code file not allowed: " + full);
			}

			entry = da->get_next();
		}
		da->list_dir_end();
	}

	dict["errors"] = all_errors;
	dict["warnings"] = all_warnings;
	dict["scripts_checked"] = scripts_checked;
	return dict;
}

bool ModValidator::is_mod_safe(const String &p_mod_path) {
	Dictionary result = validate_mod(p_mod_path);
	return bool(result["passed"]);
}

void ModValidator::_bind_methods() {
	ClassDB::bind_method(D_METHOD("validate_script_file", "path"), &ModValidator::validate_script_file);
	ClassDB::bind_method(D_METHOD("validate_mod", "mod_path"), &ModValidator::validate_mod);
	ClassDB::bind_method(D_METHOD("is_mod_safe", "mod_path"), &ModValidator::is_mod_safe);
}

ModValidator::ModValidator() {
	singleton = this;
}

ModValidator::~ModValidator() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
