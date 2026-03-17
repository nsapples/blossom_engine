/**************************************************************************/
/*  mod_loader.h                                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#pragma once

#include "core/object/object.h"
#include "core/io/config_file.h"
#include "core/templates/hash_map.h"

class ModLoader : public Object {
	GDCLASS(ModLoader, Object);

public:
	enum ModStatus {
		MOD_STATUS_UNLOADED,
		MOD_STATUS_LOADED,
		MOD_STATUS_ENABLED,
		MOD_STATUS_ERROR,
	};

	struct ModInfo {
		String id;         // Folder or pck name (without extension).
		String name;
		String version;
		String author;
		String description;
		String path;       // Absolute path to .pck or folder.
		bool is_pck = false;
		ModStatus status = MOD_STATUS_UNLOADED;
		PackedStringArray dependencies;
		PackedStringArray optional_dependencies;
		String error_message;
		String entry_scene; // Optional autoload scene.
		String entry_script; // Optional autoload script.
	};

private:
	static ModLoader *singleton;

	HashMap<String, ModInfo> mods;
	String mods_directory = "user://mods";
	bool allow_file_replacement = false;

	void _scan_directory();
	ModInfo _parse_mod_cfg(const String &p_path, const String &p_id, bool p_is_pck);
	ModInfo _parse_pck_mod(const String &p_path, const String &p_id);
	bool _check_dependencies(const ModInfo &p_mod) const;
	void _load_mod_pack(ModInfo &p_mod);
	void _activate_mod_entry(ModInfo &p_mod);

protected:
	static void _bind_methods();

public:
	static ModLoader *get_singleton();

	// Core API.
	void scan_mods();
	void load_mod(const String &p_id);
	void unload_mod(const String &p_id);
	void load_all_mods();

	// Queries.
	PackedStringArray get_mod_list() const;
	Dictionary get_mod_info(const String &p_id) const;
	bool is_mod_loaded(const String &p_id) const;
	int get_mod_count() const;

	// Settings.
	void set_mods_directory(const String &p_path);
	String get_mods_directory() const;
	void set_allow_file_replacement(bool p_allow);
	bool get_allow_file_replacement() const;

	ModLoader();
	~ModLoader();
};

VARIANT_ENUM_CAST(ModLoader::ModStatus);
