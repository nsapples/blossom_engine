#include "register_types.h"
#include "mod_loader.h"
#include "mod_validator.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

#ifdef TOOLS_ENABLED
#include "modio_editor_plugin.h"
#include "modio_uploader.h"
#include "editor/editor_node.h"
#endif

void initialize_mod_loader_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(ModLoader);
		GDREGISTER_CLASS(ModValidator);
		Engine::get_singleton()->add_singleton(Engine::Singleton("ModLoader", memnew(ModLoader)));
		Engine::get_singleton()->add_singleton(Engine::Singleton("ModValidator", memnew(ModValidator)));
	}
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		GDREGISTER_CLASS(ModIOUploader);
		Engine::get_singleton()->add_singleton(Engine::Singleton("ModIOUploader", memnew(ModIOUploader)));
		EditorPlugins::add_by_type<ModIOEditorPlugin>();
	}
#endif
}

void uninitialize_mod_loader_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		if (ModValidator::get_singleton()) {
			memdelete(ModValidator::get_singleton());
		}
		if (ModLoader::get_singleton()) {
			memdelete(ModLoader::get_singleton());
		}
	}
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		if (ModIOUploader::get_singleton()) {
			memdelete(ModIOUploader::get_singleton());
		}
	}
#endif
}
