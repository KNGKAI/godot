#include "sandbox_narrative_graph_editor.h"

#include "core/io/resource_loader.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "editor/editor_interface.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/inspector/editor_inspector.h"
#include "scene/gui/button.h"
#include "scene/gui/file_dialog.h"
#include "scene/gui/graph_edit.h"
#include "scene/gui/graph_frame.h"
#include "scene/gui/graph_node.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/popup_menu.h"

#include "modules/sandbox_narrative/mission_data.h"

namespace {

const Color PORT_COLORS[] = { Color(0.70, 0.85, 1), Color(1, 0.72, 0.32), Color(0.80, 0.60, 1) };

String node_key(const StringName &p_id) {
	// Runtime IDs may contain path separators and characters forbidden in Node names.
	CharString bytes = String(p_id).utf8();
	return "node_" + String::hex_encode_buffer((const uint8_t *)bytes.get_data(), bytes.length());
}

void add_port(GraphNode *p_node, const String &p_label, int p_type, bool p_input, bool p_output, bool p_parallel = false) {
	Label *label = memnew(Label(p_label));
	p_node->add_child(label);
	Color output = PORT_COLORS[p_type];
	// The connection shader uses the outgoing vertex alpha to mark parallel wires.
	output.a = p_parallel ? 0.75f : 1.0f;
	// GraphNode indexes port rows separately from its internal title bar.
	p_node->set_slot(label->get_index(false), p_input, p_type, PORT_COLORS[p_type], p_output, p_type, output);
}

Variant pending(const Dictionary &p_changes, const Ref<Resource> &p_resource, const StringName &p_field) {
	Dictionary fields = p_changes.get(p_resource, Dictionary());
	return fields.get(p_field, p_resource->get(p_field));
}

void change(Dictionary &r_changes, const Ref<Resource> &p_resource, const StringName &p_field, const Variant &p_value) {
	Dictionary fields = r_changes.get(p_resource, Dictionary());
	fields[p_field] = p_value;
	r_changes[p_resource] = fields;
}

void retain_on_canvas(Dictionary &r_meta, const StringName &p_key, const Ref<Resource> &p_resource, const GraphElement *p_element) {
	Dictionary layout = r_meta.get(p_key, Dictionary());
	layout["position"] = p_element->get_position_offset();
	layout["resource"] = p_resource;
	r_meta[p_key] = layout;
}

StringName runtime_id(const Ref<Resource> &p_resource) {
	Ref<ObjectiveData> objective = p_resource;
	if (objective.is_valid()) {
		return objective->get_objective_id();
	}
	Ref<SequenceAction> action = p_resource;
	return action.is_valid() ? action->get_step_uuid() : StringName();
}

bool is_array_gate(const Ref<Resource> &p_resource) {
	return Object::cast_to<CondAll>(p_resource.ptr()) || Object::cast_to<CondAny>(p_resource.ptr());
}

bool is_descendant(const StringName &p_class, const StringName &p_base) {
	StringName native = ScriptServer::is_global_class(p_class) ? ScriptServer::get_global_class_native_base(p_class) : p_class;
	return ClassDB::class_exists(native) && ClassDB::is_parent_class(native, p_base);
}

} // namespace

void SandboxNarrativeGraphEditor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("edit", "resource"), &SandboxNarrativeGraphEditor::edit);
	ClassDB::bind_method(D_METHOD("get_graph_edit"), &SandboxNarrativeGraphEditor::get_graph_edit);
	ClassDB::bind_method(D_METHOD("get_edited_resource"), &SandboxNarrativeGraphEditor::get_edited_resource);
	ClassDB::bind_method(D_METHOD("add_graph_node", "type", "position"), &SandboxNarrativeGraphEditor::add_graph_node);
	ADD_SIGNAL(MethodInfo("resource_selected", PropertyInfo(Variant::OBJECT, "resource", PROPERTY_HINT_RESOURCE_TYPE, "Resource")));
}

SandboxNarrativeGraphEditor::SandboxNarrativeGraphEditor(UndoRedo *p_standalone_history) :
		standalone_history(p_standalone_history) {
	HBoxContainer *bar = memnew(HBoxContainer);
	add_child(bar);
	palette = memnew(MenuButton);
	palette->set_text(TTR("Add Node"));
	palette->set_disabled(true);
	bar->add_child(palette);
	palette->connect("about_to_popup", callable_mp(this, &SandboxNarrativeGraphEditor::configure_palette));
	palette->get_popup()->connect("index_pressed", callable_mp(this, &SandboxNarrativeGraphEditor::palette_selected));
	Button *arrange = memnew(Button(TTR("Auto-Arrange (Layered DAG)")));
	bar->add_child(arrange);
	canvas = memnew(GraphEdit);
	canvas->set_v_size_flags(SIZE_EXPAND_FILL);
	canvas->set_show_arrange_button(false);
	canvas->set_right_disconnects(true);
	for (int i = FLOW; i <= SEQUENCE; i++) {
		canvas->add_valid_connection_type(i, i);
	}
	add_child(canvas);
	arrange->connect("pressed", callable_mp(canvas, &GraphEdit::arrange_nodes));
	canvas->connect("connection_request", callable_mp(this, &SandboxNarrativeGraphEditor::request_link).bind(true));
	canvas->connect("disconnection_request", callable_mp(this, &SandboxNarrativeGraphEditor::request_link).bind(false));
	canvas->connect("delete_nodes_request", callable_mp(this, &SandboxNarrativeGraphEditor::remove_nodes));
	canvas->connect("end_node_move", callable_mp(this, &SandboxNarrativeGraphEditor::save_layout));
	canvas->connect("graph_elements_linked_to_frame_request", callable_mp(this, &SandboxNarrativeGraphEditor::attach_nodes));
	canvas->connect("node_selected", callable_mp(this, &SandboxNarrativeGraphEditor::select_resource));
	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(R"(
shader_type canvas_item;
uniform vec4 rim_color : source_color;
uniform int from_type;
uniform int to_type;
uniform float line_width;
void fragment() {
	float width = 1.5 / max(line_width, 1.0);
	float edge = 0.5 - abs(UV.y - 0.5);
	float dash = 1.0;
	if (from_type == 0 && COLOR.a < 0.999) {
		float along = UV.x / max(fwidth(UV.x), 0.000001);
		dash = smoothstep(0.0, 1.0, mod(along, 14.0) - 4.0);
	}
	COLOR = vec4(mix(rim_color.rgb, COLOR.rgb, smoothstep(width, width * 2.0, edge)),
			smoothstep(0.0, width, edge) * dash);
}
)");
	canvas->override_connections_shader(shader);
	sequence_dialog = memnew(FileDialog);
	sequence_dialog->set_file_mode(FileDialog::FILE_MODE_OPEN_FILE);
	sequence_dialog->add_filter("*.tres,*.res", TTR("Sequence Resource"));
	sequence_dialog->connect("file_selected", callable_mp(this, &SandboxNarrativeGraphEditor::load_sequence));
	add_child(sequence_dialog);
}

void SandboxNarrativeGraphEditor::edit(const Ref<Resource> &p_resource) {
	if (p_resource != edited) {
		traces.clear();
	}
	edited = p_resource;
	if (!Object::cast_to<MissionData>(edited.ptr()) && !Object::cast_to<SequenceData>(edited.ptr())) {
		edited.unref();
	}
	refresh();
}

void SandboxNarrativeGraphEditor::configure_palette() {
	PopupMenu *menu = palette->get_popup();
	menu->clear();
	const bool mission = Object::cast_to<MissionData>(edited.ptr());
	LocalVector<StringName> classes;
	ClassDB::get_inheriters_from_class("SandboxCondition", classes);
	if (mission) {
		classes.push_back("ObjectiveData");
		classes.push_back("SequenceData");
	} else {
		LocalVector<StringName> actions;
		ClassDB::get_inheriters_from_class("SequenceAction", actions);
		for (const StringName &action : actions) {
			classes.push_back(action);
		}
		classes.push_back("SequenceAction");
	}
	LocalVector<StringName> scripts;
	ScriptServer::get_global_class_list(scripts);
	for (const StringName &script : scripts) {
		if (!ScriptServer::is_global_class_abstract(script) && (is_descendant(script, "SandboxCondition") || (!mission && is_descendant(script, "SequenceAction")))) {
			classes.push_back(script);
		}
	}
	classes.sort_custom<StringName::AlphCompare>();
	for (const StringName &type : classes) {
		if (ScriptServer::is_global_class(type) || ClassDB::can_instantiate(type)) {
			menu->add_item(type);
			menu->set_item_metadata(-1, type);
		}
	}
	menu->add_separator();
	menu->add_item(TTR("Comment Frame"));
	menu->set_item_metadata(-1, "GraphFrame");
	if (mission) {
		menu->add_item(TTR("Link Existing Sequence…"));
		menu->set_item_metadata(-1, "load");
	}
}

Vector2 SandboxNarrativeGraphEditor::insertion_position() const {
	Vector2 position = (canvas->get_scroll_offset() + canvas->get_size() * 0.35f) / canvas->get_zoom();
	for (uint32_t i = 0; i <= elements.size(); i++) {
		bool occupied = false;
		for (const KeyValue<StringName, GraphElement *> &item : elements) {
			if (item.value->get_position_offset().distance_squared_to(position) < 32 * 32) {
				occupied = true;
				break;
			}
		}
		if (!occupied) {
			break;
		}
		position += Vector2(40, 40);
	}
	return position;
}

void SandboxNarrativeGraphEditor::palette_selected(int p_index) {
	StringName type = palette->get_popup()->get_item_metadata(p_index);
	if (type == "load") {
		sequence_dialog->popup_centered_ratio(0.65);
	} else {
		(void)add_graph_node(type, insertion_position());
	}
}

void SandboxNarrativeGraphEditor::load_sequence(const String &p_path) {
	Ref<SequenceData> sequence = ResourceLoader::load(p_path, "SequenceData");
	if (sequence.is_valid() && Object::cast_to<MissionData>(edited.ptr()) && !node_keys.has(sequence->get_instance_id())) {
		add_resource(sequence, insertion_position());
	}
}

StringName SandboxNarrativeGraphEditor::add_graph_node(const StringName &p_class, const Vector2 &p_position) {
	if (edited.is_null()) {
		return StringName();
	}
	if (p_class == "GraphFrame") {
		String key = "frame_" + Resource::generate_scene_unique_id();
		Dictionary meta = Dictionary(edited->get("_editor_graph_meta")).duplicate();
		Dictionary layout;
		layout["rect"] = Rect2(p_position, Size2(350, 220));
		layout["title"] = TTR("Comment");
		meta[key] = layout;
		Dictionary changes;
		change(changes, edited, "_editor_graph_meta", meta);
		commit(TTR("Add Comment Frame"), changes);
		return key;
	}
	bool mission = Object::cast_to<MissionData>(edited.ptr());
	if (!is_descendant(p_class, "SandboxCondition") && !(mission && (p_class == "ObjectiveData" || p_class == "SequenceData")) && !(!mission && is_descendant(p_class, "SequenceAction"))) {
		return StringName();
	}
	Ref<Script> script;
	StringName native = p_class;
	if (ScriptServer::is_global_class(p_class)) {
		script = ResourceLoader::load(ScriptServer::get_global_class_path(p_class));
		if (script.is_null() || ScriptServer::is_global_class_abstract(p_class)) {
			return StringName();
		}
		native = script->get_instance_base_type();
	}
	Ref<Resource> node = Object::cast_to<Resource>(ClassDB::instantiate(native));
	if (node.is_null()) {
		return StringName();
	}
	if (script.is_valid()) {
		node->set_script(script);
	}
	node->set_scene_unique_id(String(p_class) + "_" + Resource::generate_scene_unique_id());
	if (p_class == "ObjectiveData") {
		node->set("objective_id", node->get_scene_unique_id());
		node->set("title", TTR("Objective"));
	} else if (p_class == "SequenceData") {
		node->set("sequence_id", node->get_scene_unique_id());
	} else if (is_descendant(p_class, "SequenceAction")) {
		node->set("step_uuid", node->get_scene_unique_id());
	}
	add_resource(node, p_position);
	return runtime_id(node) == StringName() ? StringName("ref_" + node->get_scene_unique_id()) : StringName(node_key(runtime_id(node)));
}

void SandboxNarrativeGraphEditor::add_resource(const Ref<Resource> &p_node, const Vector2 &p_position) {
	Dictionary changes;
	String key;
	if (Object::cast_to<ObjectiveData>(p_node.ptr()) || Object::cast_to<SequenceAction>(p_node.ptr())) {
		bool mission = Object::cast_to<MissionData>(edited.ptr());
		StringName list = mission ? SNAME("objectives") : SNAME("steps");
		Array members = Array(edited->get(list)).duplicate();
		members.push_back(p_node);
		change(changes, edited, list, members);
		if (members.size() == 1) {
			change(changes, edited, mission ? SNAME("entry_objective_id") : SNAME("entry_step_uuid"), runtime_id(p_node));
		}
		key = node_key(runtime_id(p_node));
	} else if (p_node->is_built_in()) {
		key = "ref_" + p_node->get_scene_unique_id();
	} else {
		key = "ref_" + p_node->get_path().sha256_text();
	}
	Dictionary meta = Dictionary(edited->get("_editor_graph_meta")).duplicate();
	Dictionary layout;
	layout["position"] = p_position;
	if (runtime_id(p_node) == StringName()) {
		// Detached condition and sequence nodes exist on the canvas before their first wire.
		layout["resource"] = p_node;
	}
	meta[key] = layout;
	change(changes, edited, "_editor_graph_meta", meta);
	commit(TTR("Add Narrative Node"), changes);
}

void SandboxNarrativeGraphEditor::queue_refresh() {
	if (!editing_field && !refresh_pending) {
		refresh_pending = true;
		callable_mp(this, &SandboxNarrativeGraphEditor::refresh).call_deferred();
	}
}

void SandboxNarrativeGraphEditor::refresh() {
	refresh_pending = false;
	Callable changed = callable_mp(this, &SandboxNarrativeGraphEditor::queue_refresh);
	HashSet<StringName> selected;
	for (const KeyValue<StringName, Ref<Resource>> &item : resources) {
		if (item.value->is_connected("changed", changed)) {
			item.value->disconnect("changed", changed);
		}
	}
	canvas->clear_connections();
	for (const KeyValue<StringName, GraphElement *> &item : elements) {
		if (item.value->is_selected()) {
			selected.insert(item.key);
		}
		canvas->remove_child(item.value);
		item.value->queue_free();
	}
	elements.clear();
	resources.clear();
	node_keys.clear();
	palette->set_disabled(edited.is_null());
	if (edited.is_null()) {
		return;
	}
	Dictionary meta = edited->get("_editor_graph_meta");
	for (const Variant *k = meta.next(nullptr); k; k = meta.next(k)) {
		if (meta[*k].get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary layout = meta[*k];
		Ref<Resource> resource = layout.get("resource", Variant());
		if (resource.is_valid() && resource != edited) {
			node_keys[resource->get_instance_id()] = *k;
		}
		if (String(*k).begins_with("frame_") && layout.has("rect")) {
			show_frame(*k, layout);
		}
	}
	(void)show_resource(edited, "entry");
	Ref<MissionData> mission = edited;
	StringName entry;
	Array members;
	if (mission.is_valid()) {
		members = mission->get_objectives();
		entry = mission->get_entry_objective_id();
	} else {
		Ref<SequenceData> sequence = edited;
		members = sequence->get_steps();
		entry = sequence->get_entry_step_uuid();
	}
	for (int i = 0; i < members.size(); i++) {
		Ref<Resource> member = members[i];
		if (member.is_valid()) {
			(void)show_resource(member, node_key(runtime_id(member)));
		}
	}
	// Older resources may rely on the runner's implicit first entry.
	if (entry == StringName() && bool(edited->get("use_first_as_entry")) && !members.is_empty() && Ref<Resource>(members[0]).is_valid()) {
		entry = runtime_id(members[0]);
	}
	wire("entry", 0, node_key(entry), 0);

	if (mission.is_valid()) {
		HashMap<StringName, int> guard_count;
		for (int i = 0; i < mission->get_transitions().size(); i++) {
			Ref<MissionTransition> transition = mission->get_transitions()[i];
			if (transition.is_null()) {
				continue;
			}
			StringName source = node_key(transition->get_from_objective_id());
			StringName target = node_key(transition->get_to_objective_id());
			wire(source, transition->is_on_fail() ? 1 : 0, target, 0);
			GraphElement **element = elements.getptr(source);
			if (element) {
				GraphNode *node = Object::cast_to<GraphNode>(*element);
				int input = 3 + guard_count[source]++;
				add_port(node, vformat(TTR("Guard → %s"), transition->get_to_objective_id()), LOGIC, true, false);
				node->set_slot_metadata_left(node->get_child_count(false) - 1, transition);
				resources["transition_" + itos(i)] = transition;
				StringName guard = show_resource(transition->get_condition(), String(source) + "_guard_" + itos(i));
				wire(guard, 0, source, input);
			}
		}
	} else {
		for (int i = 0; i < members.size(); i++) {
			Ref<SequenceAction> action = members[i];
			if (action.is_null()) {
				continue;
			}
			Ref<ActionBranchCondition> branch = action;
			for (int port = 0; port < (branch.is_valid() ? 2 : 1); port++) {
				PackedStringArray next = branch.is_null() ? action->get_next_steps() : (port == 0 ? branch->get_true_steps() : branch->get_false_steps());
				for (const String &target : next) {
					wire(node_key(action->get_step_uuid()), port, node_key(target), 0);
				}
			}
		}
	}
	for (const Variant *k = meta.next(nullptr); k; k = meta.next(k)) {
		if (meta[*k].get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary layout = meta[*k];
		Ref<Resource> ref = layout.get("resource", Variant());
		if (ref.is_valid()) {
			(void)show_resource(ref, *k);
		}
	}
	for (const KeyValue<StringName, GraphElement *> &item : elements) {
		if (GraphNode *node = Object::cast_to<GraphNode>(item.value)) {
			add_fields(node, resources[item.key]);
		}
		Dictionary layout = meta.get(item.key, Dictionary());
		StringName frame = layout.get("frame", StringName());
		if (frame != StringName() && elements.has(frame) && frame != item.key) {
			canvas->attach_graph_element_to_frame(item.key, frame);
		}
		if (selected.has(item.key)) {
			item.value->set_selected(true);
		}
	}
	for (const KeyValue<StringName, Ref<Resource>> &item : resources) {
		if (!item.value->is_connected("changed", changed)) {
			item.value->connect("changed", changed);
		}
	}
	update_trace();
}

void SandboxNarrativeGraphEditor::wire(const StringName &p_from, int p_output, const StringName &p_to, int p_input) {
	if (elements.has(p_from) && elements.has(p_to)) {
		canvas->connect_node(p_from, p_output, p_to, p_input);
	}
}

StringName SandboxNarrativeGraphEditor::show_resource(const Ref<Resource> &p_node, const String &p_key) {
	if (p_node.is_null()) {
		return StringName();
	}
	const StringName *known_key = node_keys.getptr(p_node->get_instance_id());
	StringName key = known_key ? *known_key : StringName(p_key);
	if (!known_key && p_node != edited && runtime_id(p_node) == StringName() && !p_node->get_scene_unique_id().is_empty()) {
		key = "ref_" + p_node->get_scene_unique_id();
	}
	if (elements.has(key)) {
		return key;
	}
	GraphNode *node = memnew(GraphNode);
	node->set_name(key);
	node->set_custom_minimum_size(Size2(250, 0));
	node_keys[p_node->get_instance_id()] = key;
	resources[key] = p_node;
	elements[key] = node;
	canvas->add_child(node);
	Dictionary meta = edited->get("_editor_graph_meta");
	Variant stored = meta.get(key, meta.get(runtime_id(p_node), Variant()));
	Dictionary layout;
	if (stored.get_type() == Variant::DICTIONARY) {
		layout = stored;
	} else if (stored.get_type() == Variant::VECTOR2) {
		layout["position"] = stored;
	}
	int index = elements.size() - 1;
	node->set_position_offset(layout.get("position", Vector2(30 + index % 4 * 310, 40 + index / 4 * 270)));
	Ref<Script> script = p_node->get_script();
	String title = script.is_valid() && script->get_global_name() != StringName() ? String(script->get_global_name()) : p_node->get_class();
	node->set_title(title.capitalize());

	Ref<ObjectiveData> objective = p_node;
	Ref<SequenceAction> action = p_node;
	Ref<SandboxCondition> condition = p_node;
	if (p_node == edited) {
		Ref<MissionData> mission = edited;
		node->set_title(mission.is_valid() ? TTR("Mission Entry") : TTR("Sequence Entry"));
		add_port(node, mission.is_valid() ? TTR("First Objective") : TTR("First Step"), FLOW, false, true);
		if (mission.is_valid()) {
			add_port(node, TTR("On Start"), SEQUENCE, false, true);
			add_port(node, TTR("On Complete"), SEQUENCE, false, true);
			add_port(node, TTR("Prerequisite"), LOGIC, true, false);
			wire(key, 1, show_resource(mission->get_on_start_sequence(), "on_start"), 0);
			wire(key, 2, show_resource(mission->get_on_complete_sequence(), "on_complete"), 0);
			wire(show_resource(mission->get_prerequisite(), "prerequisite"), 0, key, 0);
		}
	} else if (objective.is_valid()) {
		node->set_title(objective->get_title().is_empty() ? TTR("Objective") : objective->get_title());
		add_port(node, TTR("On Complete"), FLOW, true, true);
		add_port(node, TTR("On Fail"), FLOW, false, true);
		add_port(node, TTR("Completion Condition"), LOGIC, true, false);
		add_port(node, TTR("Failure Condition"), LOGIC, true, false);
		wire(show_resource(objective->get_complete_condition(), String(key) + "_complete"), 0, key, 1);
		wire(show_resource(objective->get_fail_condition(), String(key) + "_fail"), 0, key, 2);
	} else if (action.is_valid()) {
		Ref<ActionBranchCondition> branch = action;
		bool parallel = !action->get_wait_for_completion();
		add_port(node, branch.is_valid() ? TTR("True") : (parallel ? TTR("Continue in Parallel") : TTR("Next")), FLOW, true, true, parallel);
		if (branch.is_valid()) {
			add_port(node, TTR("False"), FLOW, false, true, parallel);
			add_port(node, TTR("Branch Condition"), LOGIC, true, false);
			wire(show_resource(branch->get_condition(), String(key) + "_condition"), 0, key, 1);
		}
	} else if (condition.is_valid()) {
		bool multiple = is_array_gate(condition);
		Ref<CondNot> negate = condition;
		add_port(node, multiple ? TTR("Inputs → Result") : (negate.is_valid() ? TTR("Invert") : TTR("Result")), LOGIC, multiple || negate.is_valid(), true);
		if (multiple) {
			Array children = condition->get("children");
			for (int i = 0; i < children.size(); i++) {
				wire(show_resource(children[i], String(key) + "_child_" + itos(i)), 0, key, 0);
			}
		} else if (negate.is_valid()) {
			wire(show_resource(negate->get_child(), String(key) + "_child"), 0, key, 0);
		}
	} else {
		add_port(node, TTR("Sequence Reference"), SEQUENCE, true, false);
		if (EditorInterface::get_singleton()) {
			Button *open = memnew(Button(TTR("Edit Sequence")));
			node->add_child(open);
			open->connect("pressed", callable_mp(EditorInterface::get_singleton(), &EditorInterface::edit_resource).bind(p_node));
		}
	}
	return key;
}

void SandboxNarrativeGraphEditor::add_fields(GraphNode *p_node, const Ref<Resource> &p_resource) {
	List<PropertyInfo> properties;
	p_resource->get_property_list(&properties);
	bool narrative_fields = false;
	for (const PropertyInfo &prop : properties) {
		if (prop.usage & PROPERTY_USAGE_CATEGORY) {
			if (prop.name == "MissionData" || prop.name == "SequenceData" || prop.name == "ObjectiveData" || prop.name == "SandboxCondition" || prop.name == "SequenceAction") {
				narrative_fields = true;
			}
			continue;
		}
		if (!narrative_fields || !(prop.usage & PROPERTY_USAGE_EDITOR) || prop.type == Variant::OBJECT || prop.type == Variant::ARRAY || prop.type == Variant::PACKED_STRING_ARRAY || prop.name == "objective_id" || prop.name == "step_uuid" || prop.name == "entry_step_uuid" || prop.name == "entry_objective_id") {
			continue;
		}
		EditorProperty *editor = EditorInspector::instantiate_property_editor(p_resource.ptr(), prop.type, prop.name, prop.hint, prop.hint_string, prop.usage);
		if (editor) {
			editor->set_object_and_property(p_resource.ptr(), prop.name);
			editor->set_draw_label(true);
			editor->set_label(String(prop.name).capitalize());
			p_node->add_child(editor);
			editor->update_property();
			editor->connect("property_changed", callable_mp(this, &SandboxNarrativeGraphEditor::change_field).bind(p_resource));
		}
	}
}

void SandboxNarrativeGraphEditor::change_field(const StringName &p_property, const Variant &p_value, const StringName &p_field, bool p_changing, const Ref<Resource> &p_resource) {
	if (p_resource->get(p_property) == p_value) {
		return;
	}
	Dictionary changes;
	change(changes, p_resource, p_property, p_value);
	// Keep native text and spin controls alive throughout interactive edits.
	editing_field = p_property != SNAME("wait_for_completion");
	commit(vformat(TTR("Set %s"), p_property), changes);
	editing_field = false;
	if (p_property == SNAME("title")) {
		GraphNode *node = Object::cast_to<GraphNode>(elements[node_keys[p_resource->get_instance_id()]]);
		node->set_title(p_value);
	}
}

void SandboxNarrativeGraphEditor::select_resource(Node *p_node) {
	const Ref<Resource> *resource = resources.getptr(p_node->get_name());
	if (resource) {
		emit_signal("resource_selected", *resource);
	}
}

void SandboxNarrativeGraphEditor::show_frame(const StringName &p_key, const Dictionary &p_layout) {
	GraphFrame *frame = memnew(GraphFrame);
	frame->set_name(p_key);
	frame->set_resizable(true);
	frame->set_autoshrink_enabled(false);
	canvas->add_child(frame);
	Rect2 rect = p_layout["rect"];
	frame->set_position_offset(rect.position);
	frame->set_size(rect.size);
	LineEdit *title = memnew(LineEdit);
	title->set_text(p_layout.get("title", TTR("Comment")));
	title->set_h_size_flags(SIZE_EXPAND_FILL);
	frame->get_titlebar_hbox()->add_child(title);
	title->connect("text_submitted", callable_mp(this, &SandboxNarrativeGraphEditor::rename_frame).bind(p_key));
	frame->connect("resize_request", callable_mp(this, &SandboxNarrativeGraphEditor::resize_frame).bind(frame));
	frame->connect("resize_end", callable_mp(this, &SandboxNarrativeGraphEditor::save_layout).unbind(1));
	elements[p_key] = frame;
}

void SandboxNarrativeGraphEditor::resize_frame(const Vector2 &p_size, GraphFrame *p_frame) {
	p_frame->set_size(p_size);
}

void SandboxNarrativeGraphEditor::rename_frame(const String &p_value, const StringName &p_key) {
	Dictionary meta = Dictionary(edited->get("_editor_graph_meta")).duplicate(true);
	Dictionary layout = meta.get(p_key, Dictionary());
	layout["title"] = p_value;
	meta[p_key] = layout;
	Dictionary changes;
	change(changes, edited, "_editor_graph_meta", meta);
	commit(TTR("Rename Narrative Frame"), changes);
}

void SandboxNarrativeGraphEditor::commit(const String &p_name, const Dictionary &p_changes) {
	if (p_changes.is_empty()) {
		return;
	}
	EditorUndoRedoManager *manager = EditorUndoRedoManager::get_singleton();
	UndoRedo *history = standalone_history;
	if (history) {
		history->create_action(p_name);
	} else {
		ERR_FAIL_NULL(manager);
		manager->create_action(p_name, UndoRedo::MERGE_DISABLE, edited.ptr());
		int id = manager->get_history_id_for_object(edited.ptr());
		history = manager->get_history_undo_redo(id);
	}
	for (const Variant *key = p_changes.next(); key; key = p_changes.next(key)) {
		Ref<Resource> resource = *key;
		Dictionary fields = p_changes[*key];
		for (const Variant *field = fields.next(); field; field = fields.next(field)) {
			Variant value = fields[*field];
			if (StringName(*field) == "_editor_graph_meta") {
				Dictionary(value).sort();
			}
			history->add_do_property(resource.ptr(), *field, value);
			history->add_undo_property(resource.ptr(), *field, resource->get(*field));
		}
	}
	// Mark the containing .tres dirty when editing a nested objective, condition, or action.
	history->add_do_method(callable_mp(edited.ptr(), &Resource::emit_changed));
	history->add_undo_method(callable_mp(edited.ptr(), &Resource::emit_changed));
	if (standalone_history) {
		history->commit_action();
	} else {
		manager->commit_action();
	}
}

void SandboxNarrativeGraphEditor::request_link(const StringName &p_from, int p_output, const StringName &p_to, int p_input, bool p_connect) {
	if (!resources.has(p_from) || !resources.has(p_to) || p_from == p_to || !elements.has(p_from) || !elements.has(p_to)) {
		return;
	}
	GraphNode *from = Object::cast_to<GraphNode>(elements[p_from]);
	GraphNode *to = Object::cast_to<GraphNode>(elements[p_to]);
	if (!from || !to || p_output < 0 || p_output >= from->get_output_port_count() || p_input < 0 || p_input >= to->get_input_port_count()) {
		return;
	}
	int type = from->get_output_port_type(p_output);
	if (type != to->get_input_port_type(p_input) || canvas->is_node_connected(p_from, p_output, p_to, p_input) == p_connect) {
		return;
	}
	// Reject cycles before they reach recursive condition evaluation or sequence execution.
	if (p_connect && (type == LOGIC || (type == FLOW && Object::cast_to<SequenceData>(edited.ptr())))) {
		LocalVector<StringName> pending_nodes;
		HashSet<StringName> visited;
		pending_nodes.push_back(p_to);
		while (!pending_nodes.is_empty()) {
			StringName current = pending_nodes[pending_nodes.size() - 1];
			pending_nodes.resize(pending_nodes.size() - 1);
			if (current == p_from) {
				return;
			}
			if (visited.has(current)) {
				continue;
			}
			visited.insert(current);
			for (const Ref<GraphEdit::Connection> &edge : canvas->get_connections()) {
				if (edge->from_node == current && Object::cast_to<GraphNode>(elements[current])->get_output_port_type(edge->from_port) == type) {
					pending_nodes.push_back(edge->to_node);
				}
			}
		}
	}
	Dictionary changes;
	if (!p_connect && (type == LOGIC || type == SEQUENCE)) {
		StringName retained = type == LOGIC ? p_from : p_to;
		Dictionary meta = Dictionary(edited->get("_editor_graph_meta")).duplicate(true);
		retain_on_canvas(meta, retained, resources[retained], elements[retained]);
		change(changes, edited, "_editor_graph_meta", meta);
	}
	change_link(changes, p_from, p_output, p_to, p_input, p_connect);
	commit(p_connect ? TTR("Connect Narrative Nodes") : TTR("Disconnect Narrative Nodes"), changes);
}

void SandboxNarrativeGraphEditor::change_link(Dictionary &r_changes, const StringName &p_from, int p_output, const StringName &p_to, int p_input, bool p_connect) {
	Ref<Resource> source = resources[p_from];
	Ref<Resource> target = resources[p_to];
	GraphNode *input = Object::cast_to<GraphNode>(elements[p_to]);
	int type = input->get_input_port_type(p_input);
	if (type == SEQUENCE) {
		change(r_changes, edited, p_output == 1 ? SNAME("on_start_sequence") : SNAME("on_complete_sequence"), p_connect ? target : Ref<Resource>());
	} else if (type == LOGIC) {
		if (is_array_gate(target)) {
			Array children = Array(pending(r_changes, target, "children")).duplicate();
			if (p_connect) {
				children.push_back(source);
			} else {
				children.erase(source);
			}
			change(r_changes, target, "children", children);
		} else {
			StringName field;
			if (target == edited) {
				field = "prerequisite";
			} else if (Object::cast_to<ObjectiveData>(target.ptr())) {
				if (p_input > 2) {
					target = input->get_slot_metadata_left(input->get_input_port_slot(p_input));
					field = "condition";
				} else {
					field = p_input == 1 ? SNAME("complete_condition") : SNAME("fail_condition");
				}
			} else {
				field = Object::cast_to<CondNot>(target.ptr()) ? SNAME("child") : SNAME("condition");
			}
			change(r_changes, target, field, p_connect ? source : Ref<Resource>());
		}
	} else if (source == edited) {
		change(r_changes, edited, "use_first_as_entry", false);
		change(r_changes, edited, Object::cast_to<MissionData>(edited.ptr()) ? SNAME("entry_objective_id") : SNAME("entry_step_uuid"), p_connect ? runtime_id(target) : StringName());
	} else if (Object::cast_to<MissionData>(edited.ptr())) {
		Array transitions = Array(pending(r_changes, edited, "transitions")).duplicate();
		if (p_connect) {
			Ref<MissionTransition> transition;
			transition.instantiate();
			transition->set_from_objective_id(runtime_id(source));
			transition->set_to_objective_id(runtime_id(target));
			transition->set_on_fail(p_output == 1);
			transitions.push_back(transition);
		} else {
			for (int i = transitions.size() - 1; i >= 0; i--) {
				Ref<MissionTransition> tr = transitions[i];
				if (tr.is_valid() && tr->get_from_objective_id() == runtime_id(source) && tr->get_to_objective_id() == runtime_id(target) && tr->is_on_fail() == (p_output == 1)) {
					transitions.remove_at(i);
				}
			}
		}
		change(r_changes, edited, "transitions", transitions);
	} else {
		StringName field = "next_steps";
		if (Object::cast_to<ActionBranchCondition>(source.ptr())) {
			field = p_output == 0 ? SNAME("true_steps") : SNAME("false_steps");
		}
		PackedStringArray steps = pending(r_changes, source, field);
		if (p_connect) {
			steps.push_back(runtime_id(target));
		} else {
			steps.erase(runtime_id(target));
		}
		change(r_changes, source, field, steps);
	}
}

void SandboxNarrativeGraphEditor::remove_nodes(const TypedArray<StringName> &p_nodes) {
	HashSet<StringName> removed;
	for (int i = 0; i < p_nodes.size(); i++) {
		StringName key = p_nodes[i];
		if (key != "entry" && elements.has(key)) {
			removed.insert(key);
		}
	}
	if (removed.is_empty()) {
		return;
	}
	Dictionary changes;
	Dictionary meta = Dictionary(edited->get("_editor_graph_meta")).duplicate(true);
	for (const Ref<GraphEdit::Connection> &edge : canvas->get_connections()) {
		if (removed.has(edge->from_node) || removed.has(edge->to_node)) {
			Ref<Resource> source = resources[edge->from_node];
			if (!removed.has(edge->from_node) && Object::cast_to<SandboxCondition>(source.ptr())) {
				retain_on_canvas(meta, edge->from_node, source, elements[edge->from_node]);
			}
			change_link(changes, edge->from_node, edge->from_port, edge->to_node, edge->to_port, false);
		}
	}
	StringName list = Object::cast_to<MissionData>(edited.ptr()) ? SNAME("objectives") : SNAME("steps");
	Array members = Array(edited->get(list)).duplicate();
	for (const StringName &key : removed) {
		if (resources.has(key)) {
			members.erase(resources[key]);
		}
		meta.erase(key);
	}
	for (const Variant *k = meta.next(); k; k = meta.next(k)) {
		if (meta[*k].get_type() == Variant::DICTIONARY) {
			Dictionary layout = meta[*k];
			if (removed.has(layout.get("frame", StringName()))) {
				layout.erase("frame");
			}
		}
	}
	change(changes, edited, list, members);
	change(changes, edited, "_editor_graph_meta", meta);
	commit(TTR("Delete Narrative Nodes"), changes);
}

void SandboxNarrativeGraphEditor::save_layout() {
	if (edited.is_null()) {
		return;
	}
	Dictionary meta = Dictionary(edited->get("_editor_graph_meta")).duplicate(true);
	bool modified = false;
	for (const KeyValue<StringName, GraphElement *> &item : elements) {
		Dictionary layout = meta.get(item.key, Dictionary());
		bool frame = Object::cast_to<GraphFrame>(item.value);
		StringName field = frame ? SNAME("rect") : SNAME("position");
		Variant value = frame ? Variant(Rect2(item.value->get_position_offset(), item.value->get_size())) : Variant(item.value->get_position_offset());
		if (layout.get(field, Variant()) != value) {
			layout[field] = value;
			if (resources.has(item.key) && resources[item.key] != edited && runtime_id(resources[item.key]) == StringName()) {
				layout["resource"] = resources[item.key];
			}
			meta[item.key] = layout;
			modified = true;
		}
	}
	if (modified) {
		Dictionary changes;
		change(changes, edited, "_editor_graph_meta", meta);
		commit(TTR("Move Narrative Nodes"), changes);
	}
}

void SandboxNarrativeGraphEditor::attach_nodes(const TypedArray<StringName> &p_names, const StringName &p_frame) {
	Dictionary meta = Dictionary(edited->get("_editor_graph_meta")).duplicate(true);
	for (int i = 0; i < p_names.size(); i++) {
		StringName key = p_names[i];
		if (!elements.has(key) || key == p_frame || Object::cast_to<GraphFrame>(elements[key])) {
			continue;
		}
		Dictionary layout = meta.get(key, Dictionary());
		layout["frame"] = p_frame;
		meta[key] = layout;
	}
	Dictionary changes;
	change(changes, edited, "_editor_graph_meta", meta);
	commit(TTR("Group Narrative Nodes"), changes);
}

void SandboxNarrativeGraphEditor::apply_trace(const Array &p_trace) {
	if (edited.is_null() || p_trace.size() != 4) {
		return;
	}
	Ref<MissionData> mission = edited;
	StringName id = edited->get(mission.is_valid() ? SNAME("mission_id") : SNAME("sequence_id"));
	String state = p_trace[3];
	if (String(p_trace[0]) != (mission.is_valid() ? "mission" : "sequence") || StringName(p_trace[1]) != id || (state != "active" && state != "awaiting" && state != "completed" && state != "failed")) {
		return;
	}
	String key = node_key(p_trace[2]);
	if (elements.has(key)) {
		traces[key] = state;
		update_trace();
	}
}

void SandboxNarrativeGraphEditor::clear_trace() {
	traces.clear();
	update_trace();
}

void SandboxNarrativeGraphEditor::update_trace() {
	for (const KeyValue<StringName, GraphElement *> &item : elements) {
		String state = traces.get(item.key, "");
		Color color(1, 1, 1);
		if (state == "active") {
			color = Color(0.42, 1, 0.54);
		} else if (state == "awaiting") {
			color = Color(1, 0.72, 0.28);
		} else if (state == "completed") {
			color = Color(0.35, 0.68, 0.72);
		} else if (state == "failed") {
			color = Color(1, 0.36, 0.32);
		}
		item.value->set_self_modulate(color);
		if (state.is_empty()) {
			if (item.value->has_meta("trace_state")) {
				item.value->remove_meta("trace_state");
			}
		} else {
			item.value->set_meta("trace_state", state);
		}
	}
	for (const Ref<GraphEdit::Connection> &edge : canvas->get_connections()) {
		float activity = traces.get(edge->from_node, "") == "completed" && traces.has(edge->to_node) ? 0.65f : 0.0f;
		canvas->set_connection_activity(edge->from_node, edge->from_port, edge->to_node, edge->to_port, activity);
	}
}
