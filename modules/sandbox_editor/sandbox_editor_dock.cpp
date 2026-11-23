#include "sandbox_editor_dock.h"

#include "sandbox_hlod_baker.h"
#include "sandbox_narrative_graph_editor.h"

#include "core/input/input_event.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/gui/window_wrapper.h"
#include "scene/gui/box_container.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/option_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/scroll_bar.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/texture_rect.h"
#include "scene/gui/tree.h"
#include "scene/resources/image_texture.h"

static constexpr const char *asset_types[] = { "MissionData", "SequenceData", "SimModule", "SandboxSchemaRegistry", "WorldPartitionManifest" };

void SandboxEditorDock::_scan(EditorFileSystemDirectory *p_dir) {
	if (!p_dir) {
		return;
	}
	for (int i = 0; i < p_dir->get_file_count(); i++) {
		const String path = p_dir->get_file_path(i);
		if (path.get_extension().to_lower() != "tres") {
			continue;
		}
		const StringName type = p_dir->get_file_type(i);
		for (const char *base : asset_types) {
			if (ClassDB::is_parent_class(type, base)) {
				int item = assets->add_item(path.trim_prefix("res://"));
				assets->set_item_metadata(item, path);
				assets->set_item_tooltip(item, String(type) + "\n" + path);
				break;
			}
		}
	}
	for (int i = 0; i < p_dir->get_subdir_count(); i++) {
		_scan(p_dir->get_subdir(i));
	}
}

void SandboxEditorDock::_refresh_assets() {
	assets->clear();
	_scan(EditorFileSystem::get_singleton()->get_filesystem());
}

void SandboxEditorDock::_open_asset(int p_idx) {
	Ref<Resource> resource = ResourceLoader::load(assets->get_item_metadata(p_idx));
	if (resource.is_valid()) {
		EditorInterface::get_singleton()->edit_resource(resource);
	}
}

void SandboxEditorDock::_inspect_resource(const Ref<Resource> &p_resource) {
	EditorInterface::get_singleton()->inspect_object(p_resource.ptr(), String(), true);
}

void SandboxEditorDock::_choose_asset_path(int p_type) {
	ERR_FAIL_INDEX(p_type, (int)std::size(asset_types));
	new_asset_type = asset_types[p_type];
	asset_dialog->set_current_file(String(new_asset_type).to_snake_case() + ".tres");
	asset_dialog->popup_centered_ratio(0.6);
}

void SandboxEditorDock::_float_tab(int p_screen) {
	const int index = tabs->get_current_tab();
	Control *panel = tabs->get_current_tab_control();
	WindowWrapper *wrapper = Object::cast_to<WindowWrapper>(panel);
	if (!wrapper) {
		const String tab_title = tabs->get_tab_title(index);
		wrapper = memnew(WindowWrapper);
		tabs->remove_child(panel);
		tabs->add_child(wrapper);
		tabs->move_child(wrapper, index);
		wrapper->set_wrapped_control(panel);
		wrapper->set_window_title(vformat(TTR("Sandbox — %s"), tab_title));
		wrapper->set_margins_enabled(true);
		tabs->set_tab_title(index, tab_title);
		tabs->set_current_tab(index);
	}
	wrapper->enable_window_on_screen(p_screen, true);
}

void SandboxEditorDock::_create_asset(const String &p_path) {
	Ref<Resource> resource = Ref<Resource>(Object::cast_to<Resource>(ClassDB::instantiate(new_asset_type)));
	ERR_FAIL_COND(resource.is_null());
	Error err = ResourceSaver::save(resource, p_path);
	ERR_FAIL_COND_MSG(err != OK, vformat("Could not save %s: %s.", p_path, error_names[err]));
	resource->set_path(p_path, true);
	EditorFileSystem::get_singleton()->update_file(p_path);
	EditorInterface::get_singleton()->edit_resource(resource);
}

void SandboxEditorDock::edit(const Ref<Resource> &p_resource) {
	graph->edit(p_resource);
	if (p_resource.is_valid()) {
		tabs->set_current_tab(1);
		make_visible();
	}
}

void SandboxEditorDock::set_bridge(const Ref<SandboxEditorDebuggerPlugin> &p_bridge) {
	bridge = p_bridge;
	bridge->connect("session_changed", callable_mp(this, &SandboxEditorDock::_session_changed));
	bridge->connect("packet_received", callable_mp(this, &SandboxEditorDock::_packet));
}

void SandboxEditorDock::_session_changed(bool p_started, int p_session) {
	int index = sessions->get_item_index(p_session);
	if (p_started && index < 0) {
		sessions->add_item(vformat(TTR("Debug session %d"), p_session + 1), p_session);
		if (session < 0) {
			sessions->select(sessions->get_item_index(p_session));
			_select_session(sessions->get_selected());
		}
	} else if (!p_started && index >= 0) {
		sessions->remove_item(index);
		if (p_session == session) {
			sessions->select(sessions->get_item_count() > 1 ? 1 : 0);
			_select_session(sessions->get_selected());
		}
	}
}

void SandboxEditorDock::_select_session(int p_idx) {
	// Index zero is the offline row. Real debugger session IDs start at zero.
	session = p_idx > 0 ? sessions->get_item_id(p_idx) : -1;
	command->set_editable(session >= 0);
	graph->clear_trace();
	metrics->clear();
	layers->clear();
	cell_layers.clear();
	heatmap->set_texture(Ref<Texture2D>());
	output->clear();
	suggestions->clear();
	command->clear();
	until_refresh = 0;
}

void SandboxEditorDock::_packet(const String &p_message, const Array &p_data, int p_session) {
	if (p_session != session) {
		return;
	}
	if (p_message == "sandbox:narrative_trace" && p_data.size() == 4) {
		graph->apply_trace(p_data);
	} else if (p_message == "sandbox:telemetry" && p_data.size() == 1 && p_data[0].get_type() == Variant::DICTIONARY) {
		_display_snapshot(p_data[0]);
	} else if (p_message == "sandbox:result" && p_data.size() == 1 && p_data[0].get_type() == Variant::DICTIONARY) {
		const Dictionary result = p_data[0];
		output->add_text(result.get("output", String()));
		output->add_newline();
		while (output->get_paragraph_count() > 256) {
			output->remove_paragraph(0);
		}
	} else if (p_message == "sandbox:completions" && p_data.size() == 2 && p_data[0] == Variant(command->get_text()) && p_data[1].get_type() == Variant::PACKED_STRING_ARRAY) {
		suggestions->clear();
		const PackedStringArray matches = p_data[1];
		for (const String &match : matches) {
			suggestions->add_item(match);
		}
	}
}

void SandboxEditorDock::_display_snapshot(const Dictionary &p_data) {
	double scroll = metrics->get_vscroll_bar()->get_value();
	metrics->clear();
	TreeItem *root = metrics->create_item();
	for (const Variant &key : p_data.keys()) {
		if (key == Variant("cells")) {
			continue;
		}
		TreeItem *item = metrics->create_item(root);
		item->set_text(0, key);
		const Variant &value = p_data[key];
		if (value.get_type() == Variant::DICTIONARY) {
			const Dictionary entries = value;
			for (const Variant &field : entries.keys()) {
				TreeItem *row = metrics->create_item(item);
				row->set_text(0, field.stringify());
				row->set_text(1, entries[field].stringify());
				row->set_tooltip_text(1, row->get_text(1));
			}
		} else if (value.get_type() == Variant::PACKED_STRING_ARRAY) {
			const PackedStringArray rows = value;
			for (const String &line : rows) {
				metrics->create_item(item)->set_text(1, line);
			}
		} else {
			item->set_text(1, value.stringify());
		}
	}
	metrics->get_vscroll_bar()->set_value(scroll);
	const String previous = layers->get_text();
	cell_layers = p_data.get("cells", Dictionary());
	layers->clear();
	Array names = cell_layers.keys();
	names.sort();
	for (const Variant &name : names) {
		layers->add_item(name);
		if (String(name) == previous) {
			layers->select(layers->get_item_count() - 1);
		}
	}
	_display_heatmap(layers->get_selected());
}

void SandboxEditorDock::_display_heatmap(int p_idx) {
	if (p_idx < 0 || p_idx >= layers->get_item_count()) {
		heatmap->set_texture(Ref<Texture2D>());
		return;
	}
	const Dictionary cells = cell_layers[layers->get_item_text(p_idx)];
	if (cells.is_empty()) {
		heatmap->set_texture(Ref<Texture2D>());
		return;
	}
	const Array coords = cells.keys();
	Vector2i lower = coords[0];
	Vector2i upper = lower;
	for (const Variant &coord : coords) {
		Vector2i xy = coord;
		lower = lower.min(xy);
		upper = upper.max(xy);
	}
	const double scale = MIN(1.0, 255.0 / MAX(1.0, MAX(double(upper.x) - lower.x, double(upper.y) - lower.y)));
	Ref<Image> image = Image::create_empty(1 + int((double(upper.x) - lower.x) * scale), 1 + int((double(upper.y) - lower.y) * scale), false, Image::FORMAT_RGB8);
	image->fill(Color(0.1, 0.1, 0.1));
	const Color colors[] = { Color(0.45, 0.45, 0.45), Color(0.3, 0.55, 0.85), Color(0.2, 0.75, 0.45), Color(1, 0.68, 0.25) };
	for (const Variant &coord : coords) {
		Vector2i xy = coord;
		image->set_pixel(int((double(xy.x) - lower.x) * scale), int((double(xy.y) - lower.y) * scale), colors[CLAMP(int(cells[coord]), 0, 3)]);
	}
	heatmap->set_texture(ImageTexture::create_from_image(image));
}

void SandboxEditorDock::_submit(const String &p_command) {
	if (p_command.strip_edges().is_empty()) {
		return;
	}
	if (bridge.is_valid() && bridge->send(session, "cmd", { p_command })) {
		output->add_text("> " + p_command);
		output->add_newline();
		command->clear();
	}
}

void SandboxEditorDock::_complete(const String &p_prefix) {
	suggestions->clear();
	if (!p_prefix.is_empty() && bridge.is_valid()) {
		bridge->send(session, "complete", { p_prefix });
	}
}

void SandboxEditorDock::_accept_suggestion(int p_idx) {
	command->set_text(suggestions->get_item_text(p_idx) + " ");
	command->set_caret_column(command->get_text().length());
	command->grab_focus();
	suggestions->clear();
}

void SandboxEditorDock::_command_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventKey> key = p_event;
	if (key.is_valid() && key->is_pressed() && key->get_keycode() == Key::TAB && suggestions->get_item_count()) {
		_accept_suggestion(0);
		command->accept_event();
	}
}

void SandboxEditorDock::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		EditorFileSystem::get_singleton()->connect("filesystem_changed", callable_mp(this, &SandboxEditorDock::_refresh_assets));
		_refresh_assets();
	} else if (p_what == NOTIFICATION_PROCESS && metrics->is_visible_in_tree() && session >= 0 && bridge.is_valid()) {
		until_refresh -= get_process_delta_time();
		if (until_refresh <= 0) {
			bridge->send(session, "snapshot");
			until_refresh = 1.0;
		}
	}
}

SandboxEditorDock::SandboxEditorDock() {
	set_name("Sandbox");
	set_title(TTR("Sandbox"));
	set_layout_key("Sandbox");
	set_default_slot(DOCK_SLOT_BOTTOM);
	set_available_layouts(DOCK_LAYOUT_ALL);
	VBoxContainer *column = memnew(VBoxContainer);
	add_child(column);
	HBoxContainer *toolbar = memnew(HBoxContainer);
	column->add_child(toolbar);
	sessions = memnew(OptionButton);
	toolbar->add_child(sessions);
	sessions->set_h_size_flags(SIZE_EXPAND_FILL);
	if (EditorNode::get_singleton()->is_multi_window_enabled()) {
		ScreenSelect *float_tab = memnew(ScreenSelect);
		toolbar->add_child(float_tab);
		float_tab->set_text(TTR("Float Tab"));
		float_tab->connect("request_open_in_screen", callable_mp(this, &SandboxEditorDock::_float_tab));
	}
	sessions->add_item(TTR("No debug session"), 0x7fffffff);
	sessions->set_item_disabled(0, true);
	sessions->connect("item_selected", callable_mp(this, &SandboxEditorDock::_select_session));
	tabs = memnew(TabContainer);
	column->add_child(tabs);
	tabs->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	VBoxContainer *browser = memnew(VBoxContainer);
	tabs->add_child(browser);
	tabs->set_tab_title(0, TTR("Assets"));
	MenuButton *create = memnew(MenuButton);
	create->set_text(TTR("New Resource"));
	create->set_h_size_flags(SIZE_SHRINK_BEGIN);
	browser->add_child(create);
	for (const char *type : asset_types) {
		create->get_popup()->add_item(type);
	}
	create->get_popup()->connect("index_pressed", callable_mp(this, &SandboxEditorDock::_choose_asset_path));
	asset_dialog = memnew(EditorFileDialog);
	asset_dialog->set_file_mode(FileDialog::FILE_MODE_SAVE_FILE);
	asset_dialog->add_filter("*.tres", TTR("Text Resource"));
	add_child(asset_dialog);
	asset_dialog->connect("file_selected", callable_mp(this, &SandboxEditorDock::_create_asset));
	assets = memnew(ItemList);
	assets->set_v_size_flags(SIZE_EXPAND_FILL);
	browser->add_child(assets);
	assets->connect("item_activated", callable_mp(this, &SandboxEditorDock::_open_asset));
	graph = memnew(SandboxNarrativeGraphEditor);
	tabs->add_child(graph);
	tabs->set_tab_title(1, TTR("Narrative Flow"));
	graph->connect("resource_selected", callable_mp(this, &SandboxEditorDock::_inspect_resource));
	VBoxContainer *live = memnew(VBoxContainer);
	tabs->add_child(live);
	tabs->set_tab_title(2, TTR("Live State"));
	metrics = memnew(Tree);
	live->add_child(metrics);
	metrics->set_columns(2);
	metrics->set_hide_root(true);
	metrics->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	layers = memnew(OptionButton);
	live->add_child(layers);
	layers->connect("item_selected", callable_mp(this, &SandboxEditorDock::_display_heatmap));
	heatmap = memnew(TextureRect);
	live->add_child(heatmap);
	heatmap->set_custom_minimum_size(Size2(0, 90));
	heatmap->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	heatmap->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	heatmap->set_texture_filter(CanvasItem::TEXTURE_FILTER_NEAREST);
	heatmap->set_tooltip_text(TTR("Resident cells by X/Z coordinate. Gray: loading, blue: proxy, green: mid, amber: high."));
	tabs->add_child(memnew(SandboxHLODBaker));
	tabs->set_tab_title(3, TTR("HLOD Baker"));
	VBoxContainer *console = memnew(VBoxContainer);
	tabs->add_child(console);
	tabs->set_tab_title(4, TTR("Console"));
	output = memnew(RichTextLabel);
	console->add_child(output);
	output->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	output->set_scroll_follow(true);
	command = memnew(LineEdit);
	console->add_child(command);
	command->set_placeholder(TTR("Enter a sandbox command"));
	command->set_editable(false);
	command->connect("text_submitted", callable_mp(this, &SandboxEditorDock::_submit));
	command->connect("text_changed", callable_mp(this, &SandboxEditorDock::_complete));
	command->connect("gui_input", callable_mp(this, &SandboxEditorDock::_command_input));
	suggestions = memnew(ItemList);
	console->add_child(suggestions);
	suggestions->set_custom_minimum_size(Size2(0, 55));
	suggestions->connect("item_activated", callable_mp(this, &SandboxEditorDock::_accept_suggestion));
	set_process(true);
}
