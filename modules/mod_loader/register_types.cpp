#include "register_types.h"
#include "mod_loader.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

void initialize_mod_loader_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(ModLoader);
		Engine::get_singleton()->add_singleton(Engine::Singleton("ModLoader", memnew(ModLoader)));
	}
}

void uninitialize_mod_loader_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		if (ModLoader::get_singleton()) {
			memdelete(ModLoader::get_singleton());
		}
	}
}
