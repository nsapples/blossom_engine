#include "register_types.h"

#ifdef TOOLS_ENABLED
#include "spatial_audio_deployer.h"
#endif

void initialize_spatial_audio_module(ModuleInitializationLevel p_level) {
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		SpatialAudioDeployer::deploy_if_needed();
	}
#endif
}

void uninitialize_spatial_audio_module(ModuleInitializationLevel p_level) {
}
