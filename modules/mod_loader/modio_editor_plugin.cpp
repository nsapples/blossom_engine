/**************************************************************************/
/*  modio_editor_plugin.cpp                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#ifdef TOOLS_ENABLED

#include "modio_editor_plugin.h"
#include "mod_validator.h"
#include "modio_uploader.h"

#include "core/object/callable_mp.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/split_container.h"
#include "scene/resources/style_box_flat.h"

// ===========================================================
//  ModIOMainScreen
// ===========================================================

ModIOMainScreen::ModIOMainScreen() {
	set_name("ModIO");
	set_h_size_flags(SIZE_EXPAND_FILL);
	set_v_size_flags(SIZE_EXPAND_FILL);
	_build_ui();
}

void ModIOMainScreen::_build_ui() {
	MarginContainer *margin = memnew(MarginContainer);
	margin->set_anchors_and_offsets_preset(PRESET_FULL_RECT);
	margin->add_theme_constant_override("margin_left", 16);
	margin->add_theme_constant_override("margin_right", 16);
	margin->add_theme_constant_override("margin_top", 12);
	margin->add_theme_constant_override("margin_bottom", 12);
	add_child(margin);

	ScrollContainer *scroll = memnew(ScrollContainer);
	scroll->set_h_size_flags(SIZE_EXPAND_FILL);
	scroll->set_v_size_flags(SIZE_EXPAND_FILL);
	margin->add_child(scroll);

	VBoxContainer *root = memnew(VBoxContainer);
	root->set_h_size_flags(SIZE_EXPAND_FILL);
	root->add_theme_constant_override("separation", 12);
	scroll->add_child(root);

	// Title.
	Label *title = memnew(Label);
	title->set_text("mod.io Integration");
	title->add_theme_font_size_override("font_size", 22);
	root->add_child(title);

	_build_auth_section(root);
	root->add_child(memnew(HSeparator));
	_build_upload_section(root);

	// HTTP node for uploader.
	http = memnew(HTTPRequest);
	http->set_timeout(60.0);
	add_child(http);
}

void ModIOMainScreen::_build_auth_section(VBoxContainer *p_root) {
	auth_section = memnew(VBoxContainer);
	auth_section->add_theme_constant_override("separation", 6);

	Label *header = memnew(Label);
	header->set_text("Authentication");
	header->add_theme_font_size_override("font_size", 18);
	auth_section->add_child(header);

	auth_status = memnew(RichTextLabel);
	auth_status->set_use_bbcode(true);
	auth_status->set_fit_content(true);
	auth_status->set_scroll_active(false);
	auth_status->set_text("[color=yellow]Not logged in[/color]");
	auth_section->add_child(auth_status);

	HBoxContainer *email_row = memnew(HBoxContainer);
	email_row->add_theme_constant_override("separation", 6);

	Label *email_label = memnew(Label);
	email_label->set_text("Email:");
	email_row->add_child(email_label);

	email_input = memnew(LineEdit);
	email_input->set_placeholder("your@email.com");
	email_input->set_h_size_flags(SIZE_EXPAND_FILL);
	email_row->add_child(email_input);

	send_code_btn = memnew(Button);
	send_code_btn->set_text("Send Code");
	send_code_btn->connect("pressed", callable_mp(this, &ModIOMainScreen::_on_send_code));
	email_row->add_child(send_code_btn);

	auth_section->add_child(email_row);

	HBoxContainer *code_row = memnew(HBoxContainer);
	code_row->add_theme_constant_override("separation", 6);

	Label *code_label = memnew(Label);
	code_label->set_text("Code:");
	code_row->add_child(code_label);

	code_input = memnew(LineEdit);
	code_input->set_placeholder("Security code from email");
	code_input->set_h_size_flags(SIZE_EXPAND_FILL);
	code_row->add_child(code_input);

	verify_btn = memnew(Button);
	verify_btn->set_text("Verify");
	verify_btn->connect("pressed", callable_mp(this, &ModIOMainScreen::_on_verify_code));
	code_row->add_child(verify_btn);

	auth_section->add_child(code_row);

	p_root->add_child(auth_section);
}

void ModIOMainScreen::_build_upload_section(VBoxContainer *p_root) {
	upload_section = memnew(VBoxContainer);
	upload_section->add_theme_constant_override("separation", 6);

	Label *header = memnew(Label);
	header->set_text("Upload Mod");
	header->add_theme_font_size_override("font_size", 18);
	upload_section->add_child(header);

	// Game ID.
	HBoxContainer *gid_row = memnew(HBoxContainer);
	Label *gid_label = memnew(Label);
	gid_label->set_text("Game ID:");
	gid_row->add_child(gid_label);
	game_id_input = memnew(LineEdit);
	game_id_input->set_placeholder("Your mod.io game ID");
	game_id_input->set_custom_minimum_size(Size2(200, 0));
	gid_row->add_child(game_id_input);
	upload_section->add_child(gid_row);

	// Mod path.
	HBoxContainer *path_row = memnew(HBoxContainer);
	Label *path_label = memnew(Label);
	path_label->set_text("Mod Folder:");
	path_row->add_child(path_label);
	mod_path_input = memnew(LineEdit);
	mod_path_input->set_placeholder("res://mods/my_mod/");
	mod_path_input->set_h_size_flags(SIZE_EXPAND_FILL);
	path_row->add_child(mod_path_input);
	upload_section->add_child(path_row);

	// Mod name.
	HBoxContainer *name_row = memnew(HBoxContainer);
	Label *name_label = memnew(Label);
	name_label->set_text("Mod Name:");
	name_row->add_child(name_label);
	mod_name_input = memnew(LineEdit);
	mod_name_input->set_placeholder("My Awesome Mod");
	mod_name_input->set_h_size_flags(SIZE_EXPAND_FILL);
	name_row->add_child(mod_name_input);
	upload_section->add_child(name_row);

	// Summary.
	Label *summary_label = memnew(Label);
	summary_label->set_text("Summary:");
	upload_section->add_child(summary_label);

	mod_summary_input = memnew(TextEdit);
	mod_summary_input->set_placeholder("Describe your mod...");
	mod_summary_input->set_custom_minimum_size(Size2(0, 80));
	upload_section->add_child(mod_summary_input);

	// Buttons.
	HBoxContainer *btn_row = memnew(HBoxContainer);
	btn_row->add_theme_constant_override("separation", 8);

	validate_btn = memnew(Button);
	validate_btn->set_text("Validate Mod");
	validate_btn->connect("pressed", callable_mp(this, &ModIOMainScreen::_on_validate));
	btn_row->add_child(validate_btn);

	upload_btn = memnew(Button);
	upload_btn->set_text("Upload to mod.io");
	upload_btn->connect("pressed", callable_mp(this, &ModIOMainScreen::_on_upload));
	btn_row->add_child(upload_btn);

	upload_section->add_child(btn_row);

	// Status.
	upload_status = memnew(RichTextLabel);
	upload_status->set_use_bbcode(true);
	upload_status->set_fit_content(true);
	upload_status->set_scroll_active(false);
	upload_section->add_child(upload_status);

	// Validation results.
	validation_results = memnew(RichTextLabel);
	validation_results->set_use_bbcode(true);
	validation_results->set_fit_content(true);
	validation_results->set_scroll_active(false);
	validation_results->set_custom_minimum_size(Size2(0, 100));
	upload_section->add_child(validation_results);

	p_root->add_child(upload_section);
}

// ===========================================================
//  Actions
// ===========================================================

void ModIOMainScreen::_on_send_code() {
	String email = email_input->get_text().strip_edges();
	if (email.is_empty()) {
		auth_status->set_text("[color=red]Enter your email address.[/color]");
		return;
	}

	ModIOUploader *uploader = ModIOUploader::get_singleton();
	if (!uploader) {
		return;
	}

	uploader->set_http_node(http);
	uploader->request_email_code(email);
	auth_status->set_text("[color=cyan]Sending code to " + email + "...[/color]");
}

void ModIOMainScreen::_on_verify_code() {
	String code = code_input->get_text().strip_edges();
	if (code.is_empty()) {
		auth_status->set_text("[color=red]Enter the security code from your email.[/color]");
		return;
	}

	ModIOUploader *uploader = ModIOUploader::get_singleton();
	if (!uploader) {
		return;
	}

	uploader->exchange_email_code(code);
	auth_status->set_text("[color=cyan]Verifying code...[/color]");
}

void ModIOMainScreen::_on_validate() {
	String mod_path = mod_path_input->get_text().strip_edges();
	if (mod_path.is_empty()) {
		validation_results->set_text("[color=red]Enter a mod folder path.[/color]");
		return;
	}

	ModValidator *validator = ModValidator::get_singleton();
	if (!validator) {
		return;
	}

	Dictionary result = validator->validate_mod(mod_path);
	bool passed = result["passed"];
	PackedStringArray errors = result["errors"];
	PackedStringArray warnings = result["warnings"];
	int checked = result["scripts_checked"];

	String text;
	if (passed) {
		text = vformat("[color=green]PASSED[/color] — %d scripts checked.\n", checked);
	} else {
		text = vformat("[color=red]FAILED[/color] — %d scripts checked.\n\n", checked);
	}

	for (const String &err : errors) {
		text += "[color=red]ERROR: " + err + "[/color]\n";
	}
	for (const String &warn : warnings) {
		text += "[color=yellow]WARN: " + warn + "[/color]\n";
	}

	validation_results->set_text(text);
}

void ModIOMainScreen::_on_upload() {
	ModIOUploader *uploader = ModIOUploader::get_singleton();
	if (!uploader) {
		return;
	}

	if (!uploader->is_authenticated()) {
		upload_status->set_text("[color=red]Log in first.[/color]");
		return;
	}

	String mod_path = mod_path_input->get_text().strip_edges();
	String mod_name = mod_name_input->get_text().strip_edges();
	String summary = mod_summary_input->get_text().strip_edges();
	String gid = game_id_input->get_text().strip_edges();

	if (mod_path.is_empty() || mod_name.is_empty() || summary.is_empty() || gid.is_empty()) {
		upload_status->set_text("[color=red]Fill in all fields.[/color]");
		return;
	}

	uploader->set_game_id(gid.to_int());
	uploader->set_http_node(http);

	// Connect signals if not already.
	if (!uploader->is_connected("authenticated", callable_mp(this, &ModIOMainScreen::_on_authenticated))) {
		uploader->connect("authenticated", callable_mp(this, &ModIOMainScreen::_on_authenticated));
		uploader->connect("status_changed", callable_mp(this, &ModIOMainScreen::_on_status_changed));
		uploader->connect("upload_completed", callable_mp(this, &ModIOMainScreen::_on_upload_completed));
		uploader->connect("upload_failed", callable_mp(this, &ModIOMainScreen::_on_upload_failed));
	}

	uploader->upload_mod(mod_path, mod_name, summary);
}

void ModIOMainScreen::_on_authenticated() {
	auth_status->set_text("[color=green]Logged in to mod.io[/color]");
}

void ModIOMainScreen::_on_status_changed(int p_status, const String &p_message) {
	upload_status->set_text("[color=cyan]" + p_message + "[/color]");
}

void ModIOMainScreen::_on_upload_completed(int p_mod_id) {
	upload_status->set_text(vformat("[color=green]Mod uploaded successfully! (ID: %d)[/color]", p_mod_id));
}

void ModIOMainScreen::_on_upload_failed(const String &p_error) {
	upload_status->set_text("[color=red]Upload failed: " + p_error + "[/color]");
}

void ModIOMainScreen::refresh() {
	ModIOUploader *uploader = ModIOUploader::get_singleton();
	if (uploader && uploader->is_authenticated()) {
		auth_status->set_text("[color=green]Logged in to mod.io[/color]");
	}
}

// ===========================================================
//  ModIOEditorPlugin
// ===========================================================

ModIOEditorPlugin::ModIOEditorPlugin() {
}

ModIOEditorPlugin::~ModIOEditorPlugin() {
}

void ModIOEditorPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			main_screen = memnew(ModIOMainScreen);
			EditorInterface::get_singleton()->get_editor_main_screen()->add_child(main_screen);
			make_visible(false);
		} break;

		case NOTIFICATION_EXIT_TREE: {
			if (main_screen) {
				main_screen->queue_free();
				main_screen = nullptr;
			}
		} break;
	}
}

const Ref<Texture2D> ModIOEditorPlugin::get_plugin_icon() const {
	Ref<Theme> theme = EditorInterface::get_singleton()->get_editor_theme();
	if (theme.is_valid() && theme->has_icon("AssetLib", "EditorIcons")) {
		return theme->get_icon("AssetLib", "EditorIcons");
	}
	return Ref<Texture2D>();
}

void ModIOEditorPlugin::make_visible(bool p_visible) {
	if (main_screen) {
		main_screen->set_visible(p_visible);
		if (p_visible) {
			main_screen->refresh();
		}
	}
}

#endif // TOOLS_ENABLED
