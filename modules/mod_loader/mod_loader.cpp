/**************************************************************************/
/*  mod_loader.cpp                                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#include "mod_loader.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/file_access_pack.h"
#include "core/object/class_db.h"

ModLoader *ModLoader::singleton = nullptr;

ModLoader *ModLoader::get_singleton() {
	return singleton;
}

// ===========================================================
//  Scanning
// ===========================================================

void ModLoader::scan_mods() {
	mods.clear();

	String abs_path = ProjectSettings::get_singleton()->globalize_path(mods_directory);

	Ref<DirAccess> da = DirAccess::open(abs_path);
	if (da.is_null()) {
		// Create the mods directory if it doesn't exist.
		da = DirAccess::open(ProjectSettings::get_singleton()->globalize_path("user://"));
		if (da.is_valid()) {
			da->make_dir_recursive("mods");
		}
		print_line("[ModLoader] Created mods directory at: " + abs_path);
		return;
	}

	_scan_directory();
	print_line(vformat("[ModLoader] Found %d mod(s).", mods.size()));
}

void ModLoader::_scan_directory() {
	String abs_path = ProjectSettings::get_singleton()->globalize_path(mods_directory);

	Ref<DirAccess> da = DirAccess::open(abs_path);
	if (da.is_null()) {
		return;
	}

	da->list_dir_begin();
	String entry = da->get_next();

	while (!entry.is_empty()) {
		if (entry == "." || entry == "..") {
			entry = da->get_next();
			continue;
		}

		String full_path = abs_path.path_join(entry);

		if (entry.get_extension() == "pck") {
			// PCK mod.
			String id = entry.get_basename();
			ModInfo info = _parse_pck_mod(full_path, id);
			mods.insert(id, info);
		} else if (da->current_is_dir()) {
			// Folder mod — look for mod.cfg.
			String cfg_path = full_path.path_join("mod.cfg");
			if (FileAccess::exists(cfg_path)) {
				ModInfo info = _parse_mod_cfg(cfg_path, entry, false);
				info.path = full_path;
				mods.insert(entry, info);
			}
		}

		entry = da->get_next();
	}
	da->list_dir_end();
}

ModLoader::ModInfo ModLoader::_parse_mod_cfg(const String &p_path, const String &p_id, bool p_is_pck) {
	ModInfo info;
	info.id = p_id;
	info.is_pck = p_is_pck;
	info.status = MOD_STATUS_UNLOADED;

	Ref<ConfigFile> cfg;
	cfg.instantiate();
	Error err = cfg->load(p_path);
	if (err != OK) {
		info.name = p_id;
		info.error_message = "Failed to parse mod.cfg";
		info.status = MOD_STATUS_ERROR;
		return info;
	}

	info.name = cfg->get_value("mod", "name", p_id);
	info.version = cfg->get_value("mod", "version", "1.0.0");
	info.author = cfg->get_value("mod", "author", "Unknown");
	info.description = cfg->get_value("mod", "description", "");
	info.entry_scene = cfg->get_value("mod", "entry_scene", "");
	info.entry_script = cfg->get_value("mod", "entry_script", "");

	// Dependencies.
	String deps_str = cfg->get_value("dependencies", "required", "");
	if (!deps_str.is_empty()) {
		info.dependencies = deps_str.split(",", false);
		for (int i = 0; i < info.dependencies.size(); i++) {
			info.dependencies.set(i, info.dependencies[i].strip_edges());
		}
	}

	String opt_deps_str = cfg->get_value("dependencies", "optional", "");
	if (!opt_deps_str.is_empty()) {
		info.optional_dependencies = opt_deps_str.split(",", false);
		for (int i = 0; i < info.optional_dependencies.size(); i++) {
			info.optional_dependencies.set(i, info.optional_dependencies[i].strip_edges());
		}
	}

	return info;
}

ModLoader::ModInfo ModLoader::_parse_pck_mod(const String &p_path, const String &p_id) {
	ModInfo info;
	info.id = p_id;
	info.path = p_path;
	info.is_pck = true;
	info.name = p_id;
	info.version = "1.0.0";
	info.author = "Unknown";
	info.status = MOD_STATUS_UNLOADED;

	// Try to load the PCK temporarily to read mod.cfg from it.
	// For now, use the ID as the name — mod.cfg inside PCK will be read after loading.
	return info;
}

// ===========================================================
//  Loading
// ===========================================================

bool ModLoader::_check_dependencies(const ModInfo &p_mod) const {
	for (const String &dep : p_mod.dependencies) {
		String dep_id = dep.split(":")[0].strip_edges();
		if (!mods.has(dep_id) || mods[dep_id].status < MOD_STATUS_LOADED) {
			return false;
		}
	}
	return true;
}

void ModLoader::_load_mod_pack(ModInfo &p_mod) {
	if (p_mod.is_pck) {
		Error err = PackedData::get_singleton()->add_pack(p_mod.path, allow_file_replacement, 0ULL);
		if (err != OK) {
			p_mod.status = MOD_STATUS_ERROR;
			p_mod.error_message = "Failed to load PCK file.";
			print_line(vformat("[ModLoader] ERROR: Failed to load PCK: %s", p_mod.path));
			return;
		}
	} else {
		// For folder mods, try loading as a pack directory.
		Error err = PackedData::get_singleton()->add_pack(p_mod.path, allow_file_replacement, 0ULL);
		if (err != OK) {
			print_line(vformat("[ModLoader] Mod folder registered (no pack): %s", p_mod.path));
		}
	}

	p_mod.status = MOD_STATUS_LOADED;
	print_line(vformat("[ModLoader] Loaded: %s v%s by %s", p_mod.name, p_mod.version, p_mod.author));
}

void ModLoader::_activate_mod_entry(ModInfo &p_mod) {
	if (p_mod.status != MOD_STATUS_LOADED) {
		return;
	}

	// Register entry scene/script as autoload if specified.
	if (!p_mod.entry_scene.is_empty()) {
		ProjectSettings::AutoloadInfo autoload;
		autoload.name = "Mod_" + p_mod.id;
		autoload.path = p_mod.entry_scene;
		autoload.is_singleton = true;
		ProjectSettings::get_singleton()->add_autoload(autoload);
		print_line(vformat("[ModLoader] Registered autoload scene: %s -> %s", autoload.name, autoload.path));
	} else if (!p_mod.entry_script.is_empty()) {
		ProjectSettings::AutoloadInfo autoload;
		autoload.name = "Mod_" + p_mod.id;
		autoload.path = p_mod.entry_script;
		autoload.is_singleton = true;
		ProjectSettings::get_singleton()->add_autoload(autoload);
		print_line(vformat("[ModLoader] Registered autoload script: %s -> %s", autoload.name, autoload.path));
	}

	p_mod.status = MOD_STATUS_ENABLED;
	emit_signal("mod_loaded", p_mod.id);
}

void ModLoader::load_mod(const String &p_id) {
	if (!mods.has(p_id)) {
		print_line(vformat("[ModLoader] Mod not found: %s", p_id));
		return;
	}

	ModInfo &info = mods[p_id];

	if (info.status >= MOD_STATUS_LOADED) {
		return; // Already loaded.
	}

	if (info.status == MOD_STATUS_ERROR) {
		print_line(vformat("[ModLoader] Skipping errored mod: %s (%s)", p_id, info.error_message));
		return;
	}

	if (!_check_dependencies(info)) {
		// Try to load dependencies first.
		for (const String &dep : info.dependencies) {
			String dep_id = dep.split(":")[0].strip_edges();
			if (mods.has(dep_id) && mods[dep_id].status < MOD_STATUS_LOADED) {
				load_mod(dep_id);
			}
		}
		if (!_check_dependencies(info)) {
			info.status = MOD_STATUS_ERROR;
			info.error_message = "Missing required dependencies.";
			print_line(vformat("[ModLoader] ERROR: Missing dependencies for: %s", p_id));
			return;
		}
	}

	_load_mod_pack(info);
	_activate_mod_entry(info);
}

void ModLoader::unload_mod(const String &p_id) {
	if (!mods.has(p_id)) {
		return;
	}

	ModInfo &info = mods[p_id];

	// Remove autoload if registered.
	String autoload_name = "Mod_" + p_id;
	ProjectSettings::get_singleton()->remove_autoload(autoload_name);

	info.status = MOD_STATUS_UNLOADED;
	emit_signal("mod_unloaded", p_id);
	print_line(vformat("[ModLoader] Unloaded: %s", p_id));
}

void ModLoader::load_all_mods() {
	if (mods.is_empty()) {
		scan_mods();
	}

	// Load in dependency order — mods without deps first.
	PackedStringArray load_order;
	PackedStringArray remaining;

	for (const KeyValue<String, ModInfo> &kv : mods) {
		if (kv.value.dependencies.is_empty()) {
			load_order.push_back(kv.key);
		} else {
			remaining.push_back(kv.key);
		}
	}

	// Simple topological sort — keep loading mods whose deps are satisfied.
	int max_iterations = remaining.size() * remaining.size();
	int iteration = 0;
	while (!remaining.is_empty() && iteration < max_iterations) {
		for (int i = remaining.size() - 1; i >= 0; i--) {
			if (_check_dependencies(mods[remaining[i]])) {
				load_order.push_back(remaining[i]);
				remaining.remove_at(i);
			}
		}
		iteration++;
	}

	// Add remaining (unresolvable deps) at end — they'll fail with error.
	for (const String &r : remaining) {
		load_order.push_back(r);
	}

	for (const String &id : load_order) {
		load_mod(id);
	}
}

// ===========================================================
//  Queries
// ===========================================================

PackedStringArray ModLoader::get_mod_list() const {
	PackedStringArray list;
	for (const KeyValue<String, ModInfo> &kv : mods) {
		list.push_back(kv.key);
	}
	return list;
}

Dictionary ModLoader::get_mod_info(const String &p_id) const {
	Dictionary info;
	if (!mods.has(p_id)) {
		return info;
	}
	const ModInfo &m = mods[p_id];
	info["id"] = m.id;
	info["name"] = m.name;
	info["version"] = m.version;
	info["author"] = m.author;
	info["description"] = m.description;
	info["path"] = m.path;
	info["is_pck"] = m.is_pck;
	info["status"] = (int)m.status;
	info["error"] = m.error_message;
	info["dependencies"] = m.dependencies;
	return info;
}

bool ModLoader::is_mod_loaded(const String &p_id) const {
	return mods.has(p_id) && mods[p_id].status >= MOD_STATUS_LOADED;
}

int ModLoader::get_mod_count() const {
	return mods.size();
}

// ===========================================================
//  Settings
// ===========================================================

void ModLoader::set_mods_directory(const String &p_path) {
	mods_directory = p_path;
}

String ModLoader::get_mods_directory() const {
	return mods_directory;
}

void ModLoader::set_allow_file_replacement(bool p_allow) {
	allow_file_replacement = p_allow;
}

bool ModLoader::get_allow_file_replacement() const {
	return allow_file_replacement;
}

// ===========================================================
//  Binding
// ===========================================================

void ModLoader::_bind_methods() {
	ClassDB::bind_method(D_METHOD("scan_mods"), &ModLoader::scan_mods);
	ClassDB::bind_method(D_METHOD("load_mod", "id"), &ModLoader::load_mod);
	ClassDB::bind_method(D_METHOD("unload_mod", "id"), &ModLoader::unload_mod);
	ClassDB::bind_method(D_METHOD("load_all_mods"), &ModLoader::load_all_mods);

	ClassDB::bind_method(D_METHOD("get_mod_list"), &ModLoader::get_mod_list);
	ClassDB::bind_method(D_METHOD("get_mod_info", "id"), &ModLoader::get_mod_info);
	ClassDB::bind_method(D_METHOD("is_mod_loaded", "id"), &ModLoader::is_mod_loaded);
	ClassDB::bind_method(D_METHOD("get_mod_count"), &ModLoader::get_mod_count);

	ClassDB::bind_method(D_METHOD("set_mods_directory", "path"), &ModLoader::set_mods_directory);
	ClassDB::bind_method(D_METHOD("get_mods_directory"), &ModLoader::get_mods_directory);
	ClassDB::bind_method(D_METHOD("set_allow_file_replacement", "allow"), &ModLoader::set_allow_file_replacement);
	ClassDB::bind_method(D_METHOD("get_allow_file_replacement"), &ModLoader::get_allow_file_replacement);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "mods_directory"), "set_mods_directory", "get_mods_directory");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "allow_file_replacement"), "set_allow_file_replacement", "get_allow_file_replacement");

	ADD_SIGNAL(MethodInfo("mod_loaded", PropertyInfo(Variant::STRING, "id")));
	ADD_SIGNAL(MethodInfo("mod_unloaded", PropertyInfo(Variant::STRING, "id")));

	BIND_ENUM_CONSTANT(MOD_STATUS_UNLOADED);
	BIND_ENUM_CONSTANT(MOD_STATUS_LOADED);
	BIND_ENUM_CONSTANT(MOD_STATUS_ENABLED);
	BIND_ENUM_CONSTANT(MOD_STATUS_ERROR);
}

ModLoader::ModLoader() {
	singleton = this;
}

ModLoader::~ModLoader() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
