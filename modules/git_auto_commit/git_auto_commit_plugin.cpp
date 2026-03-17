/**************************************************************************/
/*  git_auto_commit_plugin.cpp                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#ifdef TOOLS_ENABLED

#include "git_auto_commit_plugin.h"

#include "core/config/project_settings.h"
#include "core/os/os.h"
#include "editor/editor_interface.h"
#include "editor/editor_string_names.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/check_box.h"
#include "scene/gui/dialogs.h"

// ===========================================================
//  GitUtils
// ===========================================================

String GitUtils::get_project_path() const {
	return ProjectSettings::get_singleton()->globalize_path("res://");
}

Dictionary GitUtils::run(const PackedStringArray &p_args) const {
	List<String> args;
	args.push_back("-C");
	args.push_back(get_project_path());
	for (const String &a : p_args) {
		args.push_back(a);
	}
	String output;
	int exit_code = OS::get_singleton()->execute("git", args, &output);
	Dictionary result;
	result["exit_code"] = exit_code;
	result["output"] = output.strip_edges();
	return result;
}

bool GitUtils::has_repo() const {
	Dictionary r = run({ "rev-parse", "--is-inside-work-tree" });
	return int(r["exit_code"]) == 0;
}

String GitUtils::get_remote_url() const {
	String remote = GLOBAL_GET("git_auto_commit/remote");
	if (remote.is_empty()) {
		remote = "origin";
	}
	Dictionary r = run({ "remote", "get-url", remote });
	return int(r["exit_code"]) == 0 ? String(r["output"]) : "";
}

String GitUtils::get_gh_user() const {
	List<String> args;
	args.push_back("auth");
	args.push_back("status");
	args.push_back("--active");
	String output;
	int exit_code = OS::get_singleton()->execute("gh", args, &output);
	output = output.strip_edges();
	if (exit_code != 0) {
		return "";
	}
	int idx = output.find("account ");
	if (idx == -1) {
		return "";
	}
	String after = output.substr(idx + 8);
	int space = after.find(" ");
	return space > 0 ? after.substr(0, space) : after;
}

String GitUtils::get_current_branch() const {
	Dictionary r = run({ "branch", "--show-current" });
	return int(r["exit_code"]) == 0 ? String(r["output"]) : "";
}

String GitUtils::get_status_porcelain() const {
	Dictionary r = run({ "status", "--porcelain" });
	return int(r["exit_code"]) == 0 ? String(r["output"]) : "";
}

Dictionary GitUtils::stage_all() {
	return run({ "add", "-A" });
}

Dictionary GitUtils::commit(const String &p_msg) {
	return run({ "commit", "-m", p_msg });
}

Vector<Dictionary> GitUtils::get_branches() const {
	Dictionary r = run({ "branch", "--no-color" });
	Vector<Dictionary> branches;
	if (int(r["exit_code"]) != 0) {
		return branches;
	}
	String output = r["output"];
	PackedStringArray lines = output.split("\n");
	for (const String &line : lines) {
		String trimmed = line.strip_edges();
		if (trimmed.is_empty()) {
			continue;
		}
		Dictionary b;
		b["current"] = trimmed.begins_with("*");
		b["name"] = trimmed.trim_prefix("*").strip_edges();
		branches.push_back(b);
	}
	return branches;
}

Dictionary GitUtils::switch_branch(const String &p_branch) {
	return run({ "checkout", p_branch });
}

Dictionary GitUtils::create_branch(const String &p_branch) {
	return run({ "checkout", "-b", p_branch });
}

Dictionary GitUtils::fetch() {
	String remote = GLOBAL_GET("git_auto_commit/remote");
	if (remote.is_empty()) {
		remote = "origin";
	}
	return run({ "fetch", remote });
}

Dictionary GitUtils::get_behind_ahead() const {
	String remote = GLOBAL_GET("git_auto_commit/remote");
	if (remote.is_empty()) {
		remote = "origin";
	}
	String branch = GLOBAL_GET("git_auto_commit/branch");
	if (branch.is_empty()) {
		branch = "master";
	}
	Dictionary r = run({ "rev-list", "--count", "--left-right", vformat("%s/%s...HEAD", remote, branch) });
	Dictionary result;
	result["behind"] = 0;
	result["ahead"] = 0;
	if (int(r["exit_code"]) != 0) {
		return result;
	}
	String output = r["output"];
	int tab = output.find("\t");
	if (tab == -1) {
		return result;
	}
	result["behind"] = output.substr(0, tab).to_int();
	result["ahead"] = output.substr(tab + 1).to_int();
	return result;
}

Dictionary GitUtils::push() {
	String remote = GLOBAL_GET("git_auto_commit/remote");
	if (remote.is_empty()) {
		remote = "origin";
	}
	String branch = GLOBAL_GET("git_auto_commit/branch");
	if (branch.is_empty()) {
		branch = "master";
	}
	return run({ "push", remote, branch });
}

Dictionary GitUtils::undo_last_commit() {
	return run({ "reset", "--soft", "HEAD~1" });
}

PackedStringArray GitUtils::get_all_tags() const {
	Dictionary r = run({ "tag", "-l", "--sort=-creatordate" });
	PackedStringArray tags;
	if (int(r["exit_code"]) != 0) {
		return tags;
	}
	String output = r["output"];
	if (output.is_empty()) {
		return tags;
	}
	PackedStringArray lines = output.split("\n");
	for (const String &line : lines) {
		String trimmed = line.strip_edges();
		if (!trimmed.is_empty()) {
			tags.push_back(trimmed);
		}
	}
	return tags;
}

HashMap<String, PackedStringArray> GitUtils::get_tag_map() const {
	Dictionary r = run({ "tag", "-l", "--format=%(objectname:short) %(refname:short)" });
	HashMap<String, PackedStringArray> tag_map;
	if (int(r["exit_code"]) != 0) {
		return tag_map;
	}
	String output = r["output"];
	if (output.is_empty()) {
		return tag_map;
	}
	PackedStringArray lines = output.split("\n");
	for (const String &line : lines) {
		String trimmed = line.strip_edges();
		if (trimmed.is_empty()) {
			continue;
		}
		int space = trimmed.find(" ");
		if (space == -1) {
			continue;
		}
		String hash_val = trimmed.substr(0, space);
		String tname = trimmed.substr(space + 1);
		if (!tag_map.has(hash_val)) {
			tag_map.insert(hash_val, PackedStringArray());
		}
		tag_map[hash_val].push_back(tname);
	}
	return tag_map;
}

Dictionary GitUtils::create_tag(const String &p_name, const String &p_msg) {
	if (p_msg.is_empty()) {
		return run({ "tag", p_name });
	}
	return run({ "tag", "-a", p_name, "-m", p_msg });
}

Dictionary GitUtils::push_tag(const String &p_name) {
	String remote = GLOBAL_GET("git_auto_commit/remote");
	if (remote.is_empty()) {
		remote = "origin";
	}
	return run({ "push", remote, p_name });
}

String GitUtils::get_log(int p_count) const {
	Dictionary r = run({ "log", "--oneline", vformat("-%d", p_count) });
	return int(r["exit_code"]) == 0 ? String(r["output"]) : "";
}

String GitUtils::get_diff(bool p_staged) const {
	PackedStringArray args;
	args.push_back("diff");
	if (p_staged) {
		args.push_back("--cached");
	}
	Dictionary r = run(args);
	return int(r["exit_code"]) == 0 ? String(r["output"]) : "";
}

// ===========================================================
//  CommitAnalyzer
// ===========================================================

CommitAnalyzer::CommitAnalyzer() {
	git.instantiate();
}

String CommitAnalyzer::_extract_func_name(const String &p_content) const {
	String after = p_content.substr(5).strip_edges();
	int paren = after.find("(");
	if (paren > 0) {
		return after.substr(0, paren) + "()";
	}
	return after;
}

String CommitAnalyzer::_get_category(const String &p_path, const String &p_ext) const {
	if (p_ext == "gd" || p_ext == "cs") {
		return "Scripts";
	}
	if (p_ext == "tscn") {
		return "Scenes";
	}
	if (p_ext == "tres") {
		return "Resources";
	}
	if (p_ext == "godot" || p_path.get_file() == "project.godot") {
		return "Config";
	}
	static const char *texture_exts[] = { "png", "jpg", "jpeg", "svg", "webp", "bmp", "tga", "hdr", "exr", nullptr };
	static const char *model_exts[] = { "glb", "gltf", "fbx", "obj", "dae", "blend", nullptr };
	static const char *audio_exts[] = { "wav", "ogg", "mp3", "flac", nullptr };
	for (int i = 0; texture_exts[i]; i++) {
		if (p_ext == texture_exts[i]) {
			return "Assets";
		}
	}
	for (int i = 0; model_exts[i]; i++) {
		if (p_ext == model_exts[i]) {
			return "Assets";
		}
	}
	for (int i = 0; audio_exts[i]; i++) {
		if (p_ext == audio_exts[i]) {
			return "Assets";
		}
	}
	return "Other";
}

String CommitAnalyzer::_summarize_assets(const Vector<Dictionary> &p_files) const {
	int textures = 0, models = 0, audio = 0, other = 0;
	for (const Dictionary &f : p_files) {
		String ext = f["ext"];
		if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "svg" || ext == "webp") {
			textures++;
		} else if (ext == "glb" || ext == "gltf" || ext == "fbx" || ext == "obj") {
			models++;
		} else if (ext == "wav" || ext == "ogg" || ext == "mp3") {
			audio++;
		} else {
			other++;
		}
	}
	PackedStringArray parts;
	if (textures > 0) {
		parts.push_back(vformat("%d texture%s", textures, textures > 1 ? "s" : ""));
	}
	if (models > 0) {
		parts.push_back(vformat("%d model%s", models, models > 1 ? "s" : ""));
	}
	if (audio > 0) {
		parts.push_back(vformat("%d audio file%s", audio, audio > 1 ? "s" : ""));
	}
	if (other > 0) {
		parts.push_back(vformat("%d asset%s", other, other > 1 ? "s" : ""));
	}
	return String(", ").join(parts);
}

String CommitAnalyzer::_build_smart_title(const Vector<Dictionary> &p_files, const HashMap<String, Vector<Dictionary>> &p_categories) const {
	if (p_files.is_empty()) {
		return "Auto-commit";
	}

	if (p_files.size() == 1) {
		const Dictionary &f = p_files[0];
		String action = f["action"];
		String verb = "Update";
		if (action == "added") {
			verb = "Add";
		} else if (action == "deleted") {
			verb = "Remove";
		} else if (action == "renamed") {
			verb = "Rename";
		}
		return vformat("%s %s", verb, String(f["name"]));
	}

	if (p_categories.size() == 1) {
		auto it = p_categories.begin();
		String cat = it->key;
		int count = it->value.size();
		if (count <= 3) {
			PackedStringArray names;
			for (const Dictionary &f : it->value) {
				names.push_back(f["name"]);
			}
			return vformat("Update %s: %s", cat.to_lower(), String(", ").join(names));
		}
		return vformat("Update %d %s", count, cat.to_lower());
	}

	PackedStringArray parts;
	static const char *order[] = { "Scripts", "Scenes", "Resources", "Assets", "Config", "Other", nullptr };
	for (int i = 0; order[i]; i++) {
		if (!p_categories.has(order[i])) {
			continue;
		}
		int count = p_categories[order[i]].size();
		String cat_lower = String(order[i]).to_lower();
		parts.push_back(vformat("%d %s", count, cat_lower));
	}
	return "Update " + String(", ").join(parts);
}

String CommitAnalyzer::_build_body(const Vector<Dictionary> &p_files, const HashMap<String, Vector<Dictionary>> &p_categories, int p_noise_count) const {
	String body;
	static const char *order[] = { "Scripts", "Scenes", "Resources", "Assets", "Config", "Other", nullptr };
	for (int i = 0; order[i]; i++) {
		if (!p_categories.has(order[i])) {
			continue;
		}
		body += vformat("%s:\n", order[i]);
		for (const Dictionary &f : p_categories[order[i]]) {
			String action = f["action"];
			String icon = "~";
			if (action == "added") {
				icon = "+";
			} else if (action == "deleted") {
				icon = "-";
			} else if (action == "renamed") {
				icon = ">";
			}
			body += vformat("  %s %s\n", icon, String(f["name"]));
		}
	}
	if (p_noise_count > 0) {
		body += vformat("\n(%d auto-generated files omitted)\n", p_noise_count);
	}
	return body;
}

Dictionary CommitAnalyzer::build_commit_message(const String &p_status_text) {
	Dictionary result;
	result["title"] = "";
	result["body"] = "";

	Vector<Dictionary> files;
	PackedStringArray lines = p_status_text.split("\n");
	for (const String &line : lines) {
		String trimmed = line.strip_edges();
		if (trimmed.is_empty()) {
			continue;
		}
		String code = trimmed.substr(0, 2).strip_edges();
		String file_path = trimmed.substr(3).strip_edges();
		String action = "modified";
		if (code == "A" || code == "??") {
			action = "added";
		} else if (code == "D") {
			action = "deleted";
		} else if (code == "R") {
			action = "renamed";
		}
		String ext = file_path.get_extension().to_lower();
		// Skip noise.
		if (ext == "import" || ext == "uid") {
			continue;
		}
		Dictionary f;
		f["path"] = file_path;
		f["name"] = file_path.get_file();
		f["ext"] = ext;
		f["action"] = action;
		f["category"] = _get_category(file_path, ext);
		files.push_back(f);
	}

	int noise_count = lines.size() - files.size();

	if (files.is_empty()) {
		return result;
	}

	HashMap<String, Vector<Dictionary>> categories;
	for (const Dictionary &f : files) {
		String cat = f["category"];
		if (!categories.has(cat)) {
			categories.insert(cat, Vector<Dictionary>());
		}
		categories[cat].push_back(f);
	}

	result["title"] = _build_smart_title(files, categories);
	result["body"] = _build_body(files, categories, noise_count);
	return result;
}

// ===========================================================
//  GitToast
// ===========================================================

GitToast::GitToast() {
	set_anchors_and_offsets_preset(PRESET_BOTTOM_WIDE);
	set_offset(SIDE_TOP, -50);
	set_offset(SIDE_BOTTOM, -5);
	set_offset(SIDE_LEFT, 100);
	set_offset(SIDE_RIGHT, -100);
	set_mouse_filter(MOUSE_FILTER_IGNORE);
	set_modulate(Color(1, 1, 1, 0));

	Ref<StyleBoxFlat> style;
	style.instantiate();
	style->set_bg_color(Color(0.15, 0.15, 0.2, 0.9));
	style->set_corner_radius_all(6);
	style->set_content_margin_all(10);
	add_theme_stylebox_override("panel", style);

	label = memnew(Label);
	label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	label->set_mouse_filter(MOUSE_FILTER_IGNORE);
	add_child(label);
}

void GitToast::show_message(const String &p_msg) {
	label->set_text(p_msg);
	if (tween.is_valid()) {
		tween->kill();
	}
	set_modulate(Color(1, 1, 1, 0));
	tween = create_tween();
	tween->tween_property(this, "modulate:a", 1.0, 0.2);
	tween->tween_interval(2.5);
	tween->tween_property(this, "modulate:a", 0.0, 0.5);
}

// ===========================================================
//  GitMainScreen
// ===========================================================

GitMainScreen::GitMainScreen() {
	git.instantiate();
	set_name("GitAutoCommit");
	set_h_size_flags(SIZE_EXPAND_FILL);
	set_v_size_flags(SIZE_EXPAND_FILL);
	_build_ui();
}

void GitMainScreen::_build_ui() {
	MarginContainer *margin = memnew(MarginContainer);
	margin->set_anchors_and_offsets_preset(PRESET_FULL_RECT);
	margin->add_theme_constant_override("margin_left", 16);
	margin->add_theme_constant_override("margin_right", 16);
	margin->add_theme_constant_override("margin_top", 12);
	margin->add_theme_constant_override("margin_bottom", 12);
	add_child(margin);

	VBoxContainer *root = memnew(VBoxContainer);
	root->add_theme_constant_override("separation", 10);
	margin->add_child(root);

	_build_status_panel(root);
	_build_actions_panel(root);
	root->add_child(memnew(HSeparator));
	_build_lower_panels(root);

	toast = memnew(GitToast);
	add_child(toast);
}

void GitMainScreen::_build_status_panel(VBoxContainer *p_root) {
	PanelContainer *panel = memnew(PanelContainer);
	Ref<StyleBoxFlat> style;
	style.instantiate();
	style->set_bg_color(Color(0.12, 0.12, 0.16, 1.0));
	style->set_corner_radius_all(4);
	style->set_content_margin_all(14);
	panel->add_theme_stylebox_override("panel", style);

	VBoxContainer *vbox = memnew(VBoxContainer);
	vbox->add_theme_constant_override("separation", 4);
	panel->add_child(vbox);

	auto make_rtl = []() -> RichTextLabel * {
		RichTextLabel *lbl = memnew(RichTextLabel);
		lbl->set_use_bbcode(true);
		lbl->set_fit_content(true);
		lbl->set_scroll_active(false);
		lbl->set_h_size_flags(SIZE_EXPAND_FILL);
		return lbl;
	};

	auth_label = make_rtl();
	vbox->add_child(auth_label);
	repo_label = make_rtl();
	vbox->add_child(repo_label);
	branch_label = make_rtl();
	vbox->add_child(branch_label);

	p_root->add_child(panel);
}

void GitMainScreen::_build_actions_panel(VBoxContainer *p_root) {
	HBoxContainer *row = memnew(HBoxContainer);
	row->add_theme_constant_override("separation", 6);

	Button *push_btn = memnew(Button);
	push_btn->set_text("Push");
	push_btn->set_tooltip_text("Push commits to remote");
	push_btn->connect("pressed", callable_mp(this, &GitMainScreen::_on_push));
	row->add_child(push_btn);

	Button *undo_btn = memnew(Button);
	undo_btn->set_text("Undo Commit");
	undo_btn->set_tooltip_text("Revert last commit (keeps files)");
	undo_btn->connect("pressed", callable_mp(this, &GitMainScreen::_on_undo));
	row->add_child(undo_btn);

	Button *tag_btn = memnew(Button);
	tag_btn->set_text("Tag");
	tag_btn->set_tooltip_text("Create a release tag");
	tag_btn->connect("pressed", callable_mp(this, &GitMainScreen::_on_tag));
	row->add_child(tag_btn);

	row->add_child(memnew(VSeparator));

	Label *branch_lbl = memnew(Label);
	branch_lbl->set_text("Branch:");
	row->add_child(branch_lbl);

	branch_dropdown = memnew(OptionButton);
	branch_dropdown->set_custom_minimum_size(Size2(160, 0));
	branch_dropdown->connect("item_selected", callable_mp(this, &GitMainScreen::_on_branch_selected));
	row->add_child(branch_dropdown);

	Button *new_btn = memnew(Button);
	new_btn->set_text("+");
	new_btn->set_tooltip_text("Create new branch");
	new_btn->connect("pressed", callable_mp(this, &GitMainScreen::_on_new_branch));
	row->add_child(new_btn);

	p_root->add_child(row);
}

void GitMainScreen::_build_lower_panels(VBoxContainer *p_root) {
	HSplitContainer *hsplit = memnew(HSplitContainer);
	hsplit->set_v_size_flags(SIZE_EXPAND_FILL);
	hsplit->set_split_offset(500);
	p_root->add_child(hsplit);

	// History panel.
	VBoxContainer *history_vbox = memnew(VBoxContainer);
	history_vbox->set_h_size_flags(SIZE_EXPAND_FILL);

	HBoxContainer *hist_header = memnew(HBoxContainer);
	Label *hist_title = memnew(Label);
	hist_title->set_text("Commit History");
	hist_title->add_theme_font_size_override("font_size", 16);
	hist_title->set_h_size_flags(SIZE_EXPAND_FILL);
	hist_header->add_child(hist_title);

	Button *refresh_btn = memnew(Button);
	refresh_btn->set_text("Refresh");
	refresh_btn->connect("pressed", callable_mp(this, &GitMainScreen::refresh_all));
	hist_header->add_child(refresh_btn);
	history_vbox->add_child(hist_header);

	ScrollContainer *hist_scroll = memnew(ScrollContainer);
	hist_scroll->set_v_size_flags(SIZE_EXPAND_FILL);
	hist_scroll->set_h_size_flags(SIZE_EXPAND_FILL);
	history_list = memnew(VBoxContainer);
	history_list->set_h_size_flags(SIZE_EXPAND_FILL);
	hist_scroll->add_child(history_list);
	history_vbox->add_child(hist_scroll);

	hsplit->add_child(history_vbox);

	// Right side: diff + gitignore.
	VSplitContainer *right_split = memnew(VSplitContainer);
	right_split->set_h_size_flags(SIZE_EXPAND_FILL);
	right_split->set_split_offset(300);

	// Diff panel.
	VBoxContainer *diff_vbox = memnew(VBoxContainer);
	diff_vbox->set_v_size_flags(SIZE_EXPAND_FILL);

	HBoxContainer *diff_header = memnew(HBoxContainer);
	Label *diff_title = memnew(Label);
	diff_title->set_text("Diff Viewer");
	diff_title->add_theme_font_size_override("font_size", 16);
	diff_title->set_h_size_flags(SIZE_EXPAND_FILL);
	diff_header->add_child(diff_title);

	Button *staged_btn = memnew(Button);
	staged_btn->set_text("Staged");
	staged_btn->connect("pressed", callable_mp(this, &GitMainScreen::_on_show_diff).bind(true));
	diff_header->add_child(staged_btn);

	Button *unstaged_btn = memnew(Button);
	unstaged_btn->set_text("Unstaged");
	unstaged_btn->connect("pressed", callable_mp(this, &GitMainScreen::_on_show_diff).bind(false));
	diff_header->add_child(unstaged_btn);

	diff_vbox->add_child(diff_header);

	diff_text = memnew(TextEdit);
	diff_text->set_editable(false);
	diff_text->set_v_size_flags(SIZE_EXPAND_FILL);
	diff_text->set_h_size_flags(SIZE_EXPAND_FILL);
	diff_text->add_theme_font_size_override("font_size", 13);
	diff_vbox->add_child(diff_text);

	right_split->add_child(diff_vbox);

	// Gitignore panel.
	VBoxContainer *gi_vbox = memnew(VBoxContainer);
	gi_vbox->set_v_size_flags(SIZE_EXPAND_FILL);

	Label *gi_title = memnew(Label);
	gi_title->set_text(".gitignore");
	gi_title->add_theme_font_size_override("font_size", 16);
	gi_vbox->add_child(gi_title);

	HBoxContainer *gi_row = memnew(HBoxContainer);
	gitignore_input = memnew(LineEdit);
	gitignore_input->set_placeholder("e.g. *.tmp or builds/");
	gitignore_input->set_h_size_flags(SIZE_EXPAND_FILL);
	gi_row->add_child(gitignore_input);

	Button *gi_add_btn = memnew(Button);
	gi_add_btn->set_text("Add");
	gi_add_btn->connect("pressed", callable_mp(this, &GitMainScreen::_on_gitignore_add));
	gi_row->add_child(gi_add_btn);
	gi_vbox->add_child(gi_row);

	gi_vbox->add_child(memnew(HSeparator));

	gitignore_contents = memnew(TextEdit);
	gitignore_contents->set_v_size_flags(SIZE_EXPAND_FILL);
	gitignore_contents->set_h_size_flags(SIZE_EXPAND_FILL);
	gitignore_contents->add_theme_font_size_override("font_size", 13);
	gi_vbox->add_child(gitignore_contents);

	Button *gi_save_btn = memnew(Button);
	gi_save_btn->set_text("Save .gitignore");
	gi_save_btn->connect("pressed", callable_mp(this, &GitMainScreen::_on_gitignore_save));
	gi_vbox->add_child(gi_save_btn);

	right_split->add_child(gi_vbox);

	hsplit->add_child(right_split);
}

void GitMainScreen::refresh_all() {
	_refresh_status();
	_refresh_branches();
	_refresh_history();
	_refresh_gitignore();
}

void GitMainScreen::show_toast(const String &p_msg) {
	if (toast) {
		toast->show_message(p_msg);
	}
}

void GitMainScreen::_refresh_status() {
	String user = git->get_gh_user();
	if (user.is_empty()) {
		auth_label->set_text("[color=red]Not authenticated with GitHub[/color]");
	} else {
		auth_label->set_text(vformat("[color=green]Authenticated as[/color] [b]%s[/b]", user));
	}

	if (git->has_repo()) {
		String url = git->get_remote_url();
		if (url.is_empty()) {
			repo_label->set_text("[color=yellow]Local repo[/color] (no remote)");
		} else {
			String display = url.replace("https://github.com/", "").replace(".git", "");
			repo_label->set_text(vformat("[color=green]Repo:[/color] [b]%s[/b]", display));
		}
	} else {
		repo_label->set_text("[color=red]No git repository[/color]");
	}

	String branch = git->get_current_branch();
	if (!branch.is_empty()) {
		Dictionary ba = git->get_behind_ahead();
		String detail;
		if (int(ba["ahead"]) > 0) {
			detail += vformat(" [color=cyan]+%d ahead[/color]", int(ba["ahead"]));
		}
		if (int(ba["behind"]) > 0) {
			detail += vformat(" [color=orange]%d behind[/color]", int(ba["behind"]));
		}
		branch_label->set_text(vformat("[color=green]Branch:[/color] [b]%s[/b]%s", branch, detail));
	} else {
		branch_label->set_text("");
	}
}

void GitMainScreen::_refresh_branches() {
	branch_dropdown->clear();
	Vector<Dictionary> branches = git->get_branches();
	int current_idx = 0;
	for (int i = 0; i < branches.size(); i++) {
		branch_dropdown->add_item(branches[i]["name"]);
		if (bool(branches[i]["current"])) {
			current_idx = i;
		}
	}
	if (branches.size() > 0) {
		branch_dropdown->select(current_idx);
	}
}

void GitMainScreen::_refresh_history() {
	while (history_list->get_child_count() > 0) {
		history_list->get_child(0)->queue_free();
		history_list->remove_child(history_list->get_child(0));
	}

	HashMap<String, PackedStringArray> tag_map = git->get_tag_map();

	String log_text = git->get_log();
	if (log_text.is_empty()) {
		Label *lbl = memnew(Label);
		lbl->set_text("No commits yet.");
		history_list->add_child(lbl);
		return;
	}

	PackedStringArray lines = log_text.split("\n");
	for (const String &line : lines) {
		String trimmed = line.strip_edges();
		if (trimmed.is_empty()) {
			continue;
		}
		RichTextLabel *lbl = memnew(RichTextLabel);
		lbl->set_use_bbcode(true);
		lbl->set_fit_content(true);
		lbl->set_scroll_active(false);
		lbl->set_h_size_flags(SIZE_EXPAND_FILL);

		int space = trimmed.find(" ");
		if (space > 0) {
			String commit_hash = trimmed.substr(0, space);
			String commit_msg = trimmed.substr(space + 1);
			String tag_str;
			if (tag_map.has(commit_hash)) {
				tag_str = vformat(" [color=yellow][%s][/color]", String(", ").join(tag_map[commit_hash]));
			}
			lbl->set_text(vformat("[color=cyan]%s[/color] %s%s", commit_hash, commit_msg, tag_str));
		} else {
			lbl->set_text(trimmed);
		}
		history_list->add_child(lbl);
	}
}

void GitMainScreen::_refresh_gitignore() {
	String path = git->get_project_path().path_join(".gitignore");
	if (FileAccess::file_exists(path)) {
		Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
		if (f.is_valid()) {
			gitignore_contents->set_text(f->get_as_text());
		}
	} else {
		gitignore_contents->set_text("");
	}
}

void GitMainScreen::_on_push() {
	git->fetch();
	Dictionary ba = git->get_behind_ahead();
	if (int(ba["behind"]) > 0) {
		show_toast(vformat("Remote is %d commit(s) ahead - pull first!", int(ba["behind"])));
		return;
	}
	Dictionary r = git->push();
	if (int(r["exit_code"]) == 0) {
		show_toast("Push successful");
	} else {
		show_toast("Push failed - check output");
	}
	refresh_all();
}

void GitMainScreen::_on_undo() {
	Dictionary r = git->undo_last_commit();
	if (int(r["exit_code"]) == 0) {
		show_toast("Last commit undone (files kept)");
	} else {
		show_toast("Revert failed");
	}
	refresh_all();
}

void GitMainScreen::_on_tag() {
	// Simple tag creation via dialog.
	AcceptDialog *dialog = memnew(AcceptDialog);
	dialog->set_title("Create Tag");

	VBoxContainer *vbox = memnew(VBoxContainer);
	dialog->add_child(vbox);

	Label *name_label = memnew(Label);
	name_label->set_text("Tag name:");
	vbox->add_child(name_label);

	LineEdit *name_input = memnew(LineEdit);
	name_input->set_placeholder("e.g. v0.1, playable-demo, alpha");
	name_input->set_custom_minimum_size(Size2(350, 0));
	vbox->add_child(name_input);

	Label *msg_label = memnew(Label);
	msg_label->set_text("Description (optional):");
	vbox->add_child(msg_label);

	TextEdit *msg_input = memnew(TextEdit);
	msg_input->set_placeholder("What's in this release?");
	msg_input->set_custom_minimum_size(Size2(350, 80));
	vbox->add_child(msg_input);

	CheckBox *push_check = memnew(CheckBox);
	push_check->set_text("Push tag to remote");
	push_check->set_pressed(true);
	vbox->add_child(push_check);

	dialog->connect("confirmed", callable_mp(this, &GitMainScreen::refresh_all));

	// Store refs for the lambda via a simple approach: connect with binds.
	// For simplicity, we'll handle it in the confirmed signal with captured variables.
	Ref<GitUtils> git_ref = git;
	GitToast *toast_ref = toast;
	dialog->connect("confirmed", Callable([name_input, msg_input, push_check, git_ref, toast_ref, dialog]() {
		String tag_name = name_input->get_text().strip_edges();
		if (tag_name.is_empty()) {
			return;
		}
		Dictionary r = git_ref->create_tag(tag_name, msg_input->get_text().strip_edges());
		if (int(r["exit_code"]) != 0) {
			if (toast_ref) {
				toast_ref->show_message("Failed to create tag");
			}
			dialog->queue_free();
			return;
		}
		if (push_check->is_pressed()) {
			Dictionary pr = git_ref->push_tag(tag_name);
			if (toast_ref) {
				if (int(pr["exit_code"]) == 0) {
					toast_ref->show_message("Tagged & pushed: " + tag_name);
				} else {
					toast_ref->show_message("Tag created but push failed");
				}
			}
		} else {
			if (toast_ref) {
				toast_ref->show_message("Tagged: " + tag_name);
			}
		}
		dialog->queue_free();
	}));
	dialog->connect("canceled", Callable([dialog]() { dialog->queue_free(); }));

	EditorInterface::get_singleton()->get_base_control()->add_child(dialog);
	dialog->popup_centered();
}

void GitMainScreen::_on_branch_selected(int p_index) {
	String branch_name = branch_dropdown->get_item_text(p_index);
	Dictionary r = git->switch_branch(branch_name);
	if (int(r["exit_code"]) == 0) {
		show_toast("Switched to: " + branch_name);
	} else {
		show_toast("Failed to switch branch");
		_refresh_branches();
	}
	refresh_all();
}

void GitMainScreen::_on_new_branch() {
	AcceptDialog *dialog = memnew(AcceptDialog);
	dialog->set_title("New Branch");

	LineEdit *input = memnew(LineEdit);
	input->set_placeholder("feature/my-branch");
	input->set_custom_minimum_size(Size2(300, 0));
	dialog->add_child(input);

	Ref<GitUtils> git_ref = git;
	GitMainScreen *self = this;
	dialog->connect("confirmed", Callable([input, git_ref, self, dialog]() {
		String branch_name = input->get_text().strip_edges();
		if (branch_name.is_empty()) {
			return;
		}
		Dictionary r = git_ref->create_branch(branch_name);
		if (int(r["exit_code"]) == 0) {
			self->show_toast("Created branch: " + branch_name);
		} else {
			self->show_toast("Failed to create branch");
		}
		self->refresh_all();
		dialog->queue_free();
	}));
	dialog->connect("canceled", Callable([dialog]() { dialog->queue_free(); }));

	EditorInterface::get_singleton()->get_base_control()->add_child(dialog);
	dialog->popup_centered();
}

void GitMainScreen::_on_show_diff(bool p_staged) {
	String text = git->get_diff(p_staged);
	diff_text->set_text(text.is_empty() ? "(no changes)" : text);
}

void GitMainScreen::_on_gitignore_add() {
	String pattern = gitignore_input->get_text().strip_edges();
	if (pattern.is_empty()) {
		return;
	}
	String path = git->get_project_path().path_join(".gitignore");
	String existing;
	if (FileAccess::file_exists(path)) {
		Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
		if (f.is_valid()) {
			existing = f->get_as_text();
		}
	}
	// Check if already exists.
	PackedStringArray existing_lines = existing.split("\n");
	for (const String &line : existing_lines) {
		if (line.strip_edges() == pattern) {
			return;
		}
	}
	if (!existing.ends_with("\n") && !existing.is_empty()) {
		existing += "\n";
	}
	existing += pattern + "\n";
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
	if (f.is_valid()) {
		f->store_string(existing);
	}
	gitignore_input->set_text("");
	_refresh_gitignore();
	show_toast(vformat("Added '%s' to .gitignore", pattern));
}

void GitMainScreen::_on_gitignore_save() {
	String path = git->get_project_path().path_join(".gitignore");
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
	if (f.is_valid()) {
		f->store_string(gitignore_contents->get_text());
	}
	show_toast(".gitignore saved");
}

// ===========================================================
//  GitAutoCommitPlugin
// ===========================================================

GitAutoCommitPlugin::GitAutoCommitPlugin() {
	git.instantiate();
	analyzer.instantiate();
}

GitAutoCommitPlugin::~GitAutoCommitPlugin() {
}

void GitAutoCommitPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			// Add settings.
			if (!ProjectSettings::get_singleton()->has_setting("git_auto_commit/enabled")) {
				ProjectSettings::get_singleton()->set_setting("git_auto_commit/enabled", true);
			}
			ProjectSettings::get_singleton()->set_initial_value("git_auto_commit/enabled", true);

			if (!ProjectSettings::get_singleton()->has_setting("git_auto_commit/remote")) {
				ProjectSettings::get_singleton()->set_setting("git_auto_commit/remote", "origin");
			}
			ProjectSettings::get_singleton()->set_initial_value("git_auto_commit/remote", "origin");

			if (!ProjectSettings::get_singleton()->has_setting("git_auto_commit/branch")) {
				ProjectSettings::get_singleton()->set_setting("git_auto_commit/branch", "master");
			}
			ProjectSettings::get_singleton()->set_initial_value("git_auto_commit/branch", "master");

			if (!ProjectSettings::get_singleton()->has_setting("git_auto_commit/auto_push")) {
				ProjectSettings::get_singleton()->set_setting("git_auto_commit/auto_push", false);
			}
			ProjectSettings::get_singleton()->set_initial_value("git_auto_commit/auto_push", false);

			main_screen = memnew(GitMainScreen);
			EditorInterface::get_singleton()->get_editor_main_screen()->add_child(main_screen);
			make_visible(false);

			commit_timer = memnew(Timer);
			commit_timer->set_wait_time(2.0);
			commit_timer->set_one_shot(true);
			commit_timer->connect("timeout", callable_mp(this, &GitAutoCommitPlugin::_do_commit));
			add_child(commit_timer);

			connect("resource_saved", callable_mp(this, &GitAutoCommitPlugin::_on_resource_saved));
			connect("scene_changed", callable_mp(this, &GitAutoCommitPlugin::_on_scene_changed));
			EditorInterface::get_singleton()->get_resource_filesystem()->connect("filesystem_changed", callable_mp(this, &GitAutoCommitPlugin::_on_filesystem_changed));
		} break;

		case NOTIFICATION_EXIT_TREE: {
			if (main_screen) {
				main_screen->queue_free();
				main_screen = nullptr;
			}
			if (commit_timer) {
				commit_timer->queue_free();
				commit_timer = nullptr;
			}
		} break;
	}
}

Ref<Texture2D> GitAutoCommitPlugin::get_plugin_icon() const {
	Ref<Theme> theme = EditorInterface::get_singleton()->get_editor_theme();
	if (theme.is_valid() && theme->has_icon("VcsBranches", "EditorIcons")) {
		return theme->get_icon("VcsBranches", "EditorIcons");
	}
	if (theme.is_valid()) {
		return theme->get_icon("FileList", "EditorIcons");
	}
	return Ref<Texture2D>();
}

void GitAutoCommitPlugin::make_visible(bool p_visible) {
	if (main_screen) {
		main_screen->set_visible(p_visible);
		if (p_visible) {
			main_screen->refresh_all();
		}
	}
}

void GitAutoCommitPlugin::_schedule_commit() {
	if (!bool(GLOBAL_GET("git_auto_commit/enabled"))) {
		return;
	}
	pending_commit = true;
	commit_timer->start();
}

void GitAutoCommitPlugin::_do_commit() {
	if (!pending_commit) {
		return;
	}
	pending_commit = false;

	git->stage_all();

	String status_text = git->get_status_porcelain();
	if (status_text.is_empty()) {
		return;
	}

	Dictionary result = analyzer->build_commit_message(status_text);
	String title = result["title"];
	String body = result["body"];

	if (title.is_empty()) {
		return;
	}

	String msg = title;
	if (!String(body).is_empty()) {
		msg += "\n\n" + String(body).strip_edges();
	}

	Dictionary r = git->commit(msg);
	if (int(r["exit_code"]) == 0) {
		if (main_screen) {
			main_screen->show_toast(title);
			main_screen->refresh_all();
		}
		print_line("[GitAutoCommit] " + title);
		if (bool(GLOBAL_GET("git_auto_commit/auto_push"))) {
			git->push();
		}
	}
}

void GitAutoCommitPlugin::_on_resource_saved(const Ref<Resource> &p_resource) {
	_schedule_commit();
}

void GitAutoCommitPlugin::_on_scene_changed(Node *p_node) {
	_schedule_commit();
}

void GitAutoCommitPlugin::_on_filesystem_changed() {
	_schedule_commit();
}

#endif // TOOLS_ENABLED
