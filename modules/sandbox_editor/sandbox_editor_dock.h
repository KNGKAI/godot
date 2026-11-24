#pragma once

#include "sandbox_editor_debugger.h"

#include "editor/docks/editor_dock.h"

class EditorFileSystemDirectory;
class EditorFileDialog;
class ItemList;
class Label;
class LineEdit;
class OptionButton;
class RichTextLabel;
class SandboxNarrativeGraphEditor;
class TabContainer;
class TextureRect;
class Tree;

class SandboxEditorDock : public EditorDock {
	GDCLASS(SandboxEditorDock, EditorDock);

	TabContainer *tabs = nullptr;
	LineEdit *asset_filter = nullptr;
	OptionButton *asset_type_filter = nullptr;
	ItemList *assets = nullptr;
	EditorFileDialog *asset_dialog = nullptr;
	StringName new_asset_type;
	SandboxNarrativeGraphEditor *graph = nullptr;
	OptionButton *sessions = nullptr;
	Tree *metrics = nullptr;
	OptionButton *layers = nullptr;
	TextureRect *heatmap = nullptr;
	Dictionary cell_layers;
	LineEdit *command = nullptr;
	ItemList *suggestions = nullptr;
	RichTextLabel *output = nullptr;
	Vector<String> command_history;
	int history_offset = 0;
	String command_draft;
	Ref<SandboxEditorDebuggerPlugin> bridge;
	double until_refresh = 0;
	int session = -1;
	void _scan(EditorFileSystemDirectory *p_dir);
	void _refresh_assets();
	void _filter_assets_changed(const String &p_text);
	void _filter_type_changed(int p_idx);
	void _open_asset(int p_idx);
	void _choose_asset_path(int p_type);
	void _create_asset(const String &p_path);
	void _float_tab(int p_screen);
	void _inspect_resource(const Ref<Resource> &p_resource);
	void _session_changed(bool p_started, int p_session);
	void _select_session(int p_idx);
	void _packet(const String &p_message, const Array &p_data, int p_session);
	void _display_snapshot(const Dictionary &p_data);
	void _display_heatmap(int p_idx);
	void _submit(const String &p_command);
	void _complete(const String &p_prefix);
	void _accept_suggestion(int p_idx);
	void _command_input(const Ref<InputEvent> &p_event);

protected:
	void _notification(int p_what);
	static void _bind_methods() {}

public:
	void set_bridge(const Ref<SandboxEditorDebuggerPlugin> &p_bridge);
	void edit(const Ref<Resource> &p_resource);
	SandboxEditorDock();
};
