/**************************************************************************/
/*  git_auto_commit_plugin.h                                             */
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
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/split_container.h"
#include "scene/gui/text_edit.h"
#include "scene/main/timer.h"

class AcceptDialog;
class CheckBox;
class MarginContainer;

// ---- Git utility ----

class GitUtils : public RefCounted {
public:
	String get_project_path() const;
	Dictionary run(const PackedStringArray &p_args) const;

	bool has_repo() const;
	String get_remote_url() const;
	String get_gh_user() const;
	String get_current_branch() const;
	String get_status_porcelain() const;

	Dictionary stage_all();
	Dictionary commit(const String &p_msg);

	Vector<Dictionary> get_branches() const;
	Dictionary switch_branch(const String &p_branch);
	Dictionary create_branch(const String &p_branch);

	Dictionary fetch();
	Dictionary get_behind_ahead() const;
	Dictionary push();
	Dictionary undo_last_commit();

	PackedStringArray get_all_tags() const;
	HashMap<String, PackedStringArray> get_tag_map() const;
	Dictionary create_tag(const String &p_name, const String &p_msg);
	Dictionary push_tag(const String &p_name);

	String get_log(int p_count = 50) const;
	String get_diff(bool p_staged) const;
};

// ---- Commit analyzer ----

class CommitAnalyzer : public RefCounted {
	Ref<GitUtils> git;

	String _extract_func_name(const String &p_content) const;
	String _get_category(const String &p_path, const String &p_ext) const;
	String _build_smart_title(const Vector<Dictionary> &p_files, const HashMap<String, Vector<Dictionary>> &p_categories) const;
	String _build_body(const Vector<Dictionary> &p_files, const HashMap<String, Vector<Dictionary>> &p_categories, int p_noise_count) const;
	String _summarize_assets(const Vector<Dictionary> &p_files) const;

public:
	CommitAnalyzer();
	Dictionary build_commit_message(const String &p_status_text);
};

// ---- Toast notification ----

class GitToast : public PanelContainer {
	GDCLASS(GitToast, PanelContainer);

	Label *label = nullptr;
	Ref<Tween> tween;

public:
	void show_message(const String &p_msg);
	GitToast();
};

// ---- Main screen ----

class GitMainScreen : public Control {
	GDCLASS(GitMainScreen, Control);

	Ref<GitUtils> git;

	// Status panel.
	RichTextLabel *auth_label = nullptr;
	RichTextLabel *repo_label = nullptr;
	RichTextLabel *branch_label = nullptr;

	// Actions panel.
	OptionButton *branch_dropdown = nullptr;

	// History panel.
	VBoxContainer *history_list = nullptr;

	// Diff panel.
	TextEdit *diff_text = nullptr;

	// Gitignore panel.
	LineEdit *gitignore_input = nullptr;
	TextEdit *gitignore_contents = nullptr;

	GitToast *toast = nullptr;

	void _build_ui();
	void _build_status_panel(VBoxContainer *p_root);
	void _build_actions_panel(VBoxContainer *p_root);
	void _build_lower_panels(VBoxContainer *p_root);

	void _on_push();
	void _on_undo();
	void _on_tag();
	void _on_branch_selected(int p_index);
	void _on_new_branch();
	void _on_show_diff(bool p_staged);
	void _on_gitignore_add();
	void _on_gitignore_save();

	// Dialog callback helpers.
	AcceptDialog *tag_dialog = nullptr;
	LineEdit *tag_name_input = nullptr;
	TextEdit *tag_msg_input = nullptr;
	CheckBox *tag_push_check = nullptr;
	void _on_tag_confirmed();

	AcceptDialog *branch_dialog = nullptr;
	LineEdit *branch_name_input = nullptr;
	void _on_new_branch_confirmed();

	void _refresh_status();
	void _refresh_branches();
	void _refresh_history();
	void _refresh_gitignore();

public:
	void refresh_all();
	void show_toast(const String &p_msg);
	GitMainScreen();
};

// ---- Plugin ----

class GitAutoCommitPlugin : public EditorPlugin {
	GDCLASS(GitAutoCommitPlugin, EditorPlugin);

	Ref<GitUtils> git;
	Ref<CommitAnalyzer> analyzer;
	GitMainScreen *main_screen = nullptr;
	Timer *commit_timer = nullptr;
	bool pending_commit = false;

	void _schedule_commit();
	void _do_commit();
	void _on_resource_saved(const Ref<Resource> &p_resource);
	void _on_scene_changed(Node *p_node);
	void _on_filesystem_changed();

protected:
	void _notification(int p_what);

public:
	virtual String get_plugin_name() const override { return "Git"; }
	virtual bool has_main_screen() const override { return true; }
	virtual const Ref<Texture2D> get_plugin_icon() const override;
	virtual void make_visible(bool p_visible) override;

	GitAutoCommitPlugin();
	~GitAutoCommitPlugin();
};

#endif // TOOLS_ENABLED
