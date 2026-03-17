/**************************************************************************/
/*  register_types.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#include "register_types.h"

#ifdef TOOLS_ENABLED
#include "git_auto_commit_plugin.h"

#include "editor/editor_node.h"
#endif

void initialize_git_auto_commit_module(ModuleInitializationLevel p_level) {
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		EditorPlugins::add_by_type<GitAutoCommitPlugin>();
	}
#endif
}

void uninitialize_git_auto_commit_module(ModuleInitializationLevel p_level) {
}
