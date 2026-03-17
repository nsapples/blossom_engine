#ifdef TOOLS_ENABLED

#include "spatial_audio_deployer.h"

#include "spatial_audio_embedded.gen.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/config/project_settings.h"

static void _write_file(const String &p_path, const char *p_content) {
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::WRITE);
	if (f.is_valid()) {
		f->store_string(String::utf8(p_content));
	}
}

void SpatialAudioDeployer::deploy_if_needed() {
	const String addon_dir = "res://addons/spatial_audio_extended/";
	const String marker = addon_dir + "plugin.cfg";

	// If plugin already exists in the project, don't overwrite.
	if (FileAccess::exists(marker)) {
		return;
	}

	// Create directories.
	Ref<DirAccess> da = DirAccess::open("res://");
	if (da.is_null()) {
		return;
	}
	da->make_dir_recursive("addons/spatial_audio_extended/images");
	da->make_dir_recursive("addons/spatial_audio_extended/presets/materials");

	// Write all GDScript files.
	_write_file(addon_dir + "plugin.cfg", SPATIAL_AUDIO_PLUGIN_CFG);
	_write_file(addon_dir + "plugin.gd", SPATIAL_AUDIO_PLUGIN_GD);
	_write_file(addon_dir + "spatial_audio_player_3d.gd", SPATIAL_AUDIO_PLAYER_GD);
	_write_file(addon_dir + "acoustic_body.gd", ACOUSTIC_BODY_GD);
	_write_file(addon_dir + "acoustic_material.gd", ACOUSTIC_MATERIAL_GD);
	_write_file(addon_dir + "spatial_reflection_navigation_agent_3d.gd", SPATIAL_REFLECTION_NAV_GD);

	// Auto-enable the plugin in project settings.
	ProjectSettings::get_singleton()->set_setting("editor_plugins/enabled", PackedStringArray({ "res://addons/spatial_audio_extended/plugin.cfg" }));

	print_line("[Blossom Engine] Deployed built-in Spatial Audio Extended plugin.");
}

#endif // TOOLS_ENABLED
