/**************************************************************************/
/*  modio_editor_plugin.h                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#pragma once

#ifdef TOOLS_ENABLED

#include "editor/plugins/editor_plugin.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/separator.h"
#include "scene/gui/text_edit.h"
#include "scene/main/http_request.h"

class ModIOMainScreen : public Control {
	GDCLASS(ModIOMainScreen, Control);

	HTTPRequest *http = nullptr;

	// Create UGC UI.
	VBoxContainer *create_section = nullptr;
	LineEdit *ugc_id_input = nullptr;
	OptionButton *ugc_type_dropdown = nullptr;
	RichTextLabel *create_status = nullptr;

	// Auth UI.
	VBoxContainer *auth_section = nullptr;
	LineEdit *email_input = nullptr;
	LineEdit *code_input = nullptr;
	Button *send_code_btn = nullptr;
	Button *verify_btn = nullptr;
	RichTextLabel *auth_status = nullptr;

	// Upload UI.
	VBoxContainer *upload_section = nullptr;
	LineEdit *mod_path_input = nullptr;
	LineEdit *mod_name_input = nullptr;
	TextEdit *mod_summary_input = nullptr;
	Button *validate_btn = nullptr;
	Button *upload_btn = nullptr;
	RichTextLabel *upload_status = nullptr;

	// Validation results.
	RichTextLabel *validation_results = nullptr;

	void _build_ui();
	void _build_create_section(VBoxContainer *p_root);
	void _build_auth_section(VBoxContainer *p_root);
	void _build_upload_section(VBoxContainer *p_root);

	void _on_create_ugc();
	void _on_send_code();
	void _on_verify_code();
	void _update_locked_state();
	void _on_validate();
	void _on_upload();

	void _on_authenticated();
	void _on_status_changed(int p_status, const String &p_message);
	void _on_upload_completed(int p_mod_id);
	void _on_upload_failed(const String &p_error);

public:
	void refresh();
	ModIOMainScreen();
};

class ModIOEditorPlugin : public EditorPlugin {
	GDCLASS(ModIOEditorPlugin, EditorPlugin);

	ModIOMainScreen *main_screen = nullptr;

protected:
	void _notification(int p_what);

public:
	virtual String get_plugin_name() const override { return "Mods"; }
	virtual bool has_main_screen() const override { return true; }
	virtual const Ref<Texture2D> get_plugin_icon() const override;
	virtual void make_visible(bool p_visible) override;

	ModIOEditorPlugin();
	~ModIOEditorPlugin();
};

#endif // TOOLS_ENABLED
