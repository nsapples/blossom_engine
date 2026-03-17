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

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/object/callable_mp.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_file_system.h"
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
	title->set_text("User Generated Content");
	title->add_theme_font_size_override("font_size", 22);
	root->add_child(title);

	_build_create_section(root);
	root->add_child(memnew(HSeparator));
	_build_auth_section(root);
	root->add_child(memnew(HSeparator));
	_build_upload_section(root);

	// HTTP node for uploader.
	http = memnew(HTTPRequest);
	http->set_timeout(60.0);
	add_child(http);
}

void ModIOMainScreen::_build_create_section(VBoxContainer *p_root) {
	create_section = memnew(VBoxContainer);
	create_section->add_theme_constant_override("separation", 6);

	Label *header = memnew(Label);
	header->set_text("Create UGC");
	header->add_theme_font_size_override("font_size", 18);
	create_section->add_child(header);

	// UGC Type.
	HBoxContainer *type_row = memnew(HBoxContainer);
	type_row->add_theme_constant_override("separation", 6);

	Label *type_label = memnew(Label);
	type_label->set_text("UGC Type:");
	type_row->add_child(type_label);

	ugc_type_dropdown = memnew(OptionButton);
	ugc_type_dropdown->add_item("Level");
	ugc_type_dropdown->add_item("Gamemode");
	ugc_type_dropdown->add_item("Avatar");
	ugc_type_dropdown->add_item("Gun");
	ugc_type_dropdown->add_item("Misc");
	ugc_type_dropdown->set_custom_minimum_size(Size2(200, 0));
	type_row->add_child(ugc_type_dropdown);

	create_section->add_child(type_row);

	// UGC Name.
	HBoxContainer *name_row = memnew(HBoxContainer);
	name_row->add_theme_constant_override("separation", 6);

	Label *name_label = memnew(Label);
	name_label->set_text("Name:");
	name_row->add_child(name_label);

	ugc_id_input = memnew(LineEdit);
	ugc_id_input->set_placeholder("My Awesome Mod");
	ugc_id_input->set_h_size_flags(SIZE_EXPAND_FILL);
	name_row->add_child(ugc_id_input);

	create_section->add_child(name_row);

	// UGC Summary.
	HBoxContainer *summary_row = memnew(HBoxContainer);
	summary_row->add_theme_constant_override("separation", 6);

	Label *summary_label = memnew(Label);
	summary_label->set_text("Summary:");
	summary_row->add_child(summary_label);

	ugc_summary_input = memnew(LineEdit);
	ugc_summary_input->set_placeholder("A short description of your mod");
	ugc_summary_input->set_h_size_flags(SIZE_EXPAND_FILL);
	summary_row->add_child(ugc_summary_input);

	create_section->add_child(summary_row);

	// Logo path.
	HBoxContainer *logo_row = memnew(HBoxContainer);
	logo_row->add_theme_constant_override("separation", 6);

	Label *logo_label = memnew(Label);
	logo_label->set_text("Logo:");
	logo_row->add_child(logo_label);

	ugc_logo_input = memnew(LineEdit);
	ugc_logo_input->set_placeholder("res://icon.png (minimum 512x288)");
	ugc_logo_input->set_h_size_flags(SIZE_EXPAND_FILL);
	ugc_logo_input->set_text("res://icon.png");
	logo_row->add_child(ugc_logo_input);

	create_section->add_child(logo_row);

	// Create button.
	Button *create_btn = memnew(Button);
	create_btn->set_text("Create UGC Project");
	create_btn->connect("pressed", callable_mp(this, &ModIOMainScreen::_on_create_ugc));
	create_section->add_child(create_btn);

	// Status.
	create_status = memnew(RichTextLabel);
	create_status->set_use_bbcode(true);
	create_status->set_fit_content(true);
	create_status->set_scroll_active(false);
	create_section->add_child(create_status);

	p_root->add_child(create_section);
}

void ModIOMainScreen::_on_create_ugc() {
	String mod_name = ugc_id_input->get_text().strip_edges();
	String mod_summary = ugc_summary_input->get_text().strip_edges();

	if (mod_name.is_empty()) {
		create_status->set_text("[color=red]Enter a name for your mod.[/color]");
		return;
	}
	if (mod_summary.is_empty()) {
		create_status->set_text("[color=red]Enter a summary for your mod.[/color]");
		return;
	}

	ModIOUploader *uploader = ModIOUploader::get_singleton();
	if (!uploader || !uploader->is_authenticated()) {
		create_status->set_text("[color=red]Log in first.[/color]");
		return;
	}

	pending_ugc_type = ugc_type_dropdown->get_selected();

	// Create the mod on mod.io to get the ID.
	create_status->set_text("[color=cyan]Creating mod on mod.io...[/color]");

	if (!create_http) {
		create_http = memnew(HTTPRequest);
		create_http->set_timeout(30.0);
		add_child(create_http);
		create_http->connect("request_completed", callable_mp(this, &ModIOMainScreen::_on_create_mod_response));
	}

	String url = vformat("https://g-11342.modapi.io/v1/games/%d/mods", ModIOUploader::BLOSSOM_GAME_ID);

	// Add type as a tag.
	String type_tag;
	switch (pending_ugc_type) {
		case 0: type_tag = "level"; break;
		case 1: type_tag = "gamemode"; break;
		case 2: type_tag = "avatar"; break;
		case 3: type_tag = "gun"; break;
		default: type_tag = "misc"; break;
	}

	// mod.io requires multipart/form-data.
	String boundary = "----BlossomUGC" + String::num_int64(OS::get_singleton()->get_ticks_msec());

	PackedByteArray body;
	auto add_field = [&](const String &p_name, const String &p_value) {
		String part = vformat("--%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n\r\n%s\r\n", boundary, p_name, p_value);
		body.append_array(part.to_utf8_buffer());
	};

	add_field("name", mod_name);
	add_field("summary", mod_summary);
	add_field("visible", "0");
	add_field("tags[]", type_tag);

	// Add logo file (required by mod.io).
	String logo_path = ugc_logo_input->get_text().strip_edges();
	if (logo_path.is_empty()) {
		logo_path = "res://icon.png";
	}
	// Try res:// path first, then globalized path.
	Ref<FileAccess> logo_file = FileAccess::open(logo_path, FileAccess::READ);
	if (logo_file.is_null()) {
		String abs_logo = ProjectSettings::get_singleton()->globalize_path(logo_path);
		logo_file = FileAccess::open(abs_logo, FileAccess::READ);
	}
	if (logo_file.is_valid()) {
		uint64_t logo_size = logo_file->get_length();
		PackedByteArray logo_data;
		logo_data.resize(logo_size);
		logo_file->get_buffer(logo_data.ptrw(), logo_size);
		logo_file.unref();

		String logo_filename = logo_path.get_file();
		String logo_header = vformat("--%s\r\nContent-Disposition: form-data; name=\"logo\"; filename=\"%s\"\r\nContent-Type: image/png\r\n\r\n", boundary, logo_filename);
		body.append_array(logo_header.to_utf8_buffer());
		body.append_array(logo_data);
		body.append_array(String("\r\n").to_utf8_buffer());
	} else {
		create_status->set_text("[color=red]Logo file not found: " + logo_path + "[/color]");
		return;
	}

	String footer = "--" + boundary + "--\r\n";
	body.append_array(footer.to_utf8_buffer());

	PackedStringArray headers;
	headers.push_back("Authorization: Bearer " + uploader->get_access_token());
	headers.push_back("Content-Type: multipart/form-data; boundary=" + boundary);

	create_http->request_raw(url, headers, HTTPClient::METHOD_POST, body);
}

void ModIOMainScreen::_on_create_mod_response(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	String body_str;
	if (!p_body.is_empty()) {
		const uint8_t *r = p_body.ptr();
		body_str = String::utf8((const char *)r, p_body.size());
	}

	if (p_response_code != 201 && p_response_code != 200) {
		create_status->set_text(vformat("[color=red]Failed to create mod on mod.io (%d): %s[/color]", p_response_code, body_str));
		return;
	}

	Variant parsed = JSON::parse_string(body_str);
	if (parsed.get_type() != Variant::DICTIONARY) {
		create_status->set_text("[color=red]Invalid response from mod.io.[/color]");
		return;
	}

	Dictionary data = parsed;
	int mod_id = data.get("id", 0);
	String mod_name = data.get("name", "");

	if (mod_id == 0) {
		create_status->set_text("[color=red]No mod ID returned from mod.io.[/color]");
		return;
	}

	String ugc_id = String::num_int64(mod_id);

	String type_name;
	String type_folder;
	switch (pending_ugc_type) {
		case 0: type_name = "Level"; type_folder = "level"; break;
		case 1: type_name = "Gamemode"; type_folder = "gamemode"; break;
		case 2: type_name = "Avatar"; type_folder = "avatar"; break;
		case 3: type_name = "Gun"; type_folder = "gun"; break;
		default: type_name = "Misc"; type_folder = "misc"; break;
	}

	String folder_name = "UGC_" + ugc_id;
	String base_path = "res://ugc/" + type_folder + "/" + folder_name;
	String abs_path = ProjectSettings::get_singleton()->globalize_path(base_path);

	// Create directory structure.
	Ref<DirAccess> da = DirAccess::open("res://");
	if (da.is_null()) {
		create_status->set_text("[color=red]Failed to access project directory.[/color]");
		return;
	}

	da->make_dir_recursive("ugc/" + type_folder + "/" + folder_name + "/scenes");
	da->make_dir_recursive("ugc/" + type_folder + "/" + folder_name + "/scripts");
	da->make_dir_recursive("ugc/" + type_folder + "/" + folder_name + "/assets");

	// Generate mod.cfg with mod.io ID.
	String cfg_content = vformat(
			"[mod]\nname = \"%s\"\nversion = \"1.0.0\"\nauthor = \"\"\ndescription = \"\"\ntype = \"%s\"\nmodio_id = %d\n\n[dependencies]\nrequired = \n",
			mod_name, type_folder, mod_id);

	String cfg_path = abs_path.path_join("mod.cfg");
	Ref<FileAccess> f = FileAccess::open(cfg_path, FileAccess::WRITE);
	if (f.is_valid()) {
		f->store_string(cfg_content);
	}

	// Generate a starter script.
	String script_content = vformat(
			"extends Node\n\n## Entry point for UGC: %s (%s)\n## mod.io ID: %d\n\nfunc _ready() -> void:\n\tprint(\"[UGC] %s loaded.\")\n",
			mod_name, type_name, mod_id, mod_name);

	String script_path = abs_path.path_join("scripts/main.gd");
	Ref<FileAccess> sf = FileAccess::open(script_path, FileAccess::WRITE);
	if (sf.is_valid()) {
		sf->store_string(script_content);
	}

	// Auto-fill upload fields.
	mod_path_input->set_text(base_path);
	mod_name_input->set_text(mod_name);

	create_status->set_text(vformat("[color=green]Created UGC project:[/color] %s\n[color=cyan]mod.io ID:[/color] %d\n[color=cyan]Type:[/color] %s\n[color=cyan]Path:[/color] %s",
			folder_name, mod_id, type_name, base_path));

	// Refresh the filesystem dock.
	EditorInterface::get_singleton()->get_resource_filesystem()->scan();
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
	auth_status->set_text("[color=yellow]Not logged in — all features locked.[/color]");
	auth_section->add_child(auth_status);

	// Email row.
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

	// Code row.
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

	if (!uploader->is_connected("authenticated", callable_mp(this, &ModIOMainScreen::_on_authenticated))) {
		uploader->connect("authenticated", callable_mp(this, &ModIOMainScreen::_on_authenticated));
		uploader->connect("status_changed", callable_mp(this, &ModIOMainScreen::_on_status_changed));
		uploader->connect("upload_failed", callable_mp(this, &ModIOMainScreen::_on_upload_failed));
	}

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

void ModIOMainScreen::_update_locked_state() {
	ModIOUploader *uploader = ModIOUploader::get_singleton();
	bool logged_in = uploader && uploader->is_authenticated();

	// Lock/unlock Create UGC and Upload sections.
	if (create_section) {
		for (int i = 0; i < create_section->get_child_count(); i++) {
			Control *c = Object::cast_to<Control>(create_section->get_child(i));
			if (c) {
				c->set_modulate(logged_in ? Color(1, 1, 1, 1) : Color(1, 1, 1, 0.3));
			}
		}
	}
	if (upload_section) {
		for (int i = 0; i < upload_section->get_child_count(); i++) {
			Control *c = Object::cast_to<Control>(upload_section->get_child(i));
			if (c) {
				c->set_modulate(logged_in ? Color(1, 1, 1, 1) : Color(1, 1, 1, 0.3));
			}
		}
	}

	if (validate_btn) {
		validate_btn->set_disabled(!logged_in);
	}
	if (upload_btn) {
		upload_btn->set_disabled(!logged_in);
	}
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

	if (mod_path.is_empty() || mod_name.is_empty() || summary.is_empty()) {
		upload_status->set_text("[color=red]Fill in all fields.[/color]");
		return;
	}

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
	_update_locked_state();
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
	_update_locked_state();
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
