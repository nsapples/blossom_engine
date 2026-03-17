#include "register_types.h"

#include "core/object/class_db.h"
#include "spatial_audio_player_3d.h"

void initialize_spatial_audio_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(SpatialAudioPlayer3D);
	}
}

void uninitialize_spatial_audio_module(ModuleInitializationLevel p_level) {
}
