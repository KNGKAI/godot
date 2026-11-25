#pragma once

#include "../sandbox_export_plugin.h"
#include "../sandbox_inspector_plugin.h"

#include "core/config/project_settings.h"
#include "editor/animation/animation_track_editor.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/marker_3d.h"
#include "scene/gui/button.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/tree.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "tests/test_macros.h"

#include "modules/sandbox_narrative/mission_data.h"
#include "modules/sandbox_narrative/sandbox_actor.h"
#include "modules/sandbox_sim/sim_module.h"

namespace TestSandboxEditorSchema {

class ExportCallbacks : public SandboxExportPlugin {
public:
	using SandboxExportPlugin::_begin_customize_resources;
	using SandboxExportPlugin::_customize_resource;
};

class DeclaredSchemaModule : public SimModule {
public:
	Dictionary get_declared_schema() const override {
		Dictionary entry;
		entry["key"] = "sandbox_picker_probe.discovered";
		entry["value_type"] = Variant::BOOL;
		Array entries;
		entries.push_back(entry);
		Dictionary schema;
		schema["state_keys"] = entries;
		return schema;
	}
};

TEST_CASE("[SandboxEditor][Gate8] Discovery follows the lifetime of loaded module declarations") {
	Ref<DeclaredSchemaModule> module;
	module.instantiate();
	module->set_path("res://sandbox_picker_probe_module.tres");
	Ref<SandboxSchemaRegistry> merged = SandboxInspectorPlugin::discover_schema(nullptr);
	CHECK(merged->validate_state_key("sandbox_picker_probe.discovered", Variant::BOOL) == SandboxSchemaRegistry::VALIDATION_OK);
	CHECK_FALSE(SandboxSchemaRegistry::get_active_registry()->has_state_key("sandbox_picker_probe.discovered"));
	module.unref();
	CHECK_FALSE(SandboxInspectorPlugin::discover_schema(nullptr)->has_state_key("sandbox_picker_probe.discovered"));
}

TEST_CASE("[SandboxEditor][Gate8] Release customization strips only the exported graph layout") {
	Ref<ExportCallbacks> plugin;
	plugin.instantiate();
	CHECK_FALSE(plugin->_begin_customize_resources({}, { "debug", "template_debug" }));
	REQUIRE(plugin->_begin_customize_resources({}, { "release", "template_release" }));
	Ref<MissionData> mission;
	mission.instantiate();
	Ref<SequenceData> sequence;
	sequence.instantiate();
	Dictionary meta;
	meta["start"] = Vector2(73, 540);
	mission->set_editor_graph_meta(meta);
	sequence->set_editor_graph_meta(meta);
	mission->set_on_start_sequence(sequence);
	Ref<MissionData> export_mission = plugin->_customize_resource(mission, "res://mission.tres");
	REQUIRE(export_mission.is_valid());
	CHECK(export_mission != mission);
	CHECK(export_mission->get_editor_graph_meta().is_empty());
	CHECK(mission->get_editor_graph_meta() == meta);
	CHECK(export_mission->get_on_start_sequence() == sequence);
	Ref<SequenceData> export_sequence = plugin->_customize_resource(sequence, "");
	REQUIRE(export_sequence.is_valid());
	CHECK(export_sequence != sequence);
	CHECK(export_sequence->get_editor_graph_meta().is_empty());
	CHECK(sequence->get_editor_graph_meta() == meta);
}

TEST_CASE("[Editor][SandboxEditor][Gate8] Typed search groups names and flags missing, deprecated and incompatible keys") {
	Ref<SandboxSchemaRegistry> schema;
	schema.instantiate();
	schema->register_state_key("power.sector_a.load", Variant::FLOAT, 1.0, Vector2(), false, "Measured power");
	schema->register_state_key("power.sector_a.label", Variant::STRING);
	schema->register_state_key("power.old", Variant::FLOAT, 0, Vector2(), true);
	SandboxIdHint hint = SandboxIdHint::parse("sandbox_world_state_key:float");
	CHECK(hint.validate(schema, "gone") == SandboxSchemaRegistry::VALIDATION_MISSING_KEY);
	CHECK(hint.validate(schema, "power.old") == SandboxSchemaRegistry::VALIDATION_DEPRECATED);
	CHECK(hint.validate(schema, "power.sector_a.label") == SandboxSchemaRegistry::VALIDATION_TYPE_MISMATCH);
	PackedStringArray results = hint.search(schema, "PWSAL");
	REQUIRE(results.size() == 2);
	CHECK(results[0] == "power.sector_a.load");
	CHECK(results[1] == "power.sector_a.label");

	Ref<CondStateRange> range;
	range.instantiate();
	range->set_key("power.sector_a.label");
	Ref<SandboxInspectorPlugin> plugin;
	plugin.instantiate();
	SandboxIdPickerProperty *picker = memnew(SandboxIdPickerProperty);
	picker->setup(plugin.ptr(), hint, schema);
	picker->set_object_and_property(range.ptr(), SNAME("key"));
	picker->update_property();
	TypedArray<Node> buttons = picker->find_children("*", "Button", true, false);
	bool mismatch_on_field = false;
	for (int i = 0; i < buttons.size(); i++) {
		Button *button = Object::cast_to<Button>(buttons[i]);
		if (button->get_text() == "power.sector_a.label") {
			mismatch_on_field = !button->get_tooltip_text().is_empty();
		}
	}
	CHECK(mismatch_on_field);
	LineEdit *filter = Object::cast_to<LineEdit>(picker->find_children("*", "LineEdit", true, false)[0]);
	Tree *tree = Object::cast_to<Tree>(picker->find_children("*", "Tree", true, false)[0]);
	filter->emit_signal(SNAME("text_changed"), "PWSAL");
	TreeItem *group = tree->get_root()->get_first_child();
	REQUIRE(group);
	CHECK(group->get_text(0) == "power");
	TreeItem *sector = group->get_first_child();
	REQUIRE(sector);
	CHECK(sector->get_text(0) == "sector_a");
	TreeItem *load = sector->get_first_child();
	REQUIRE(load);
	CHECK(load->get_text(1) == Variant::get_type_name(Variant::FLOAT));
	CHECK(load->get_tooltip_text(0).contains("Measured power"));
	CHECK(tree->get_selected() == load);
	Ref<InputEventKey> down;
	down.instantiate();
	down->set_pressed(true);
	down->set_keycode(Key::DOWN);
	filter->emit_signal(SceneStringName(gui_input), down);
	TreeItem *label_item = load->get_next();
	REQUIRE(label_item);
	CHECK(tree->get_selected() == label_item);
	Ref<InputEventKey> up;
	up.instantiate();
	up->set_pressed(true);
	up->set_keycode(Key::UP);
	filter->emit_signal(SceneStringName(gui_input), up);
	CHECK(tree->get_selected() == load);
	picker->connect(SNAME("property_changed"), Callable(range.ptr(), SNAME("set")).unbind(2));
	Variant strict = GLOBAL_GET("sandbox/schema/strict_mode");
	ProjectSettings::get_singleton()->set_setting("sandbox/schema/strict_mode", true);
	filter->emit_signal(SNAME("text_submitted"), "missing.key");
	CHECK(range->get_key() == SNAME("power.sector_a.label"));
	filter->emit_signal(SNAME("text_submitted"), "PWSAL");
	CHECK(range->get_key() == SNAME("power.sector_a.load"));
	ProjectSettings::get_singleton()->set_setting("sandbox/schema/strict_mode", false);
	filter->emit_signal(SNAME("text_changed"), "unregistered");
	filter->emit_signal(SNAME("text_submitted"), "unregistered");
	CHECK(range->get_key() == SNAME("unregistered"));
	ProjectSettings::get_singleton()->set_setting("sandbox/schema/strict_mode", strict);
	memdelete(picker);
}

TEST_CASE("[Editor][SandboxEditor][Gate8] An armed timeline picker resolves the actor or marker at the viewport click") {
	SceneTree *tree = SceneTree::get_singleton();
	Node3D *scene = memnew(Node3D);
	tree->get_root()->add_child(scene);
	tree->set_edited_scene_root(scene);
	Camera3D *camera = memnew(Camera3D);
	scene->add_child(camera);
	Node3D *rig = memnew(Node3D);
	rig->set_position(Vector3(-1, 0, -5));
	scene->add_child(rig);
	SandboxActor *actor = memnew(SandboxActor);
	actor->set_actor_id("actor.pilot");
	rig->add_child(actor);
	Marker3D *marker = memnew(Marker3D);
	marker->set_name("Dock");
	marker->set_position(Vector3(1, 0, -5));
	scene->add_child(marker);
	AnimationTrackKeyEdit *proxy = memnew(AnimationTrackKeyEdit);
	proxy->animation.instantiate();
	proxy->track = proxy->animation->add_track(Animation::TYPE_ACTOR_COMMAND);
	Dictionary payload;
	payload["marker_id"] = "OldDock";
	payload["duration"] = 1.5;
	proxy->animation->actor_command_track_insert_key(proxy->track, 0, "old_actor", "move", payload);
	List<PropertyInfo> props;
	proxy->get_property_list(&props);
	Ref<SandboxInspectorPlugin> plugin;
	plugin.instantiate();
	Ref<RefCounted> changes;
	changes.instantiate();
	for (const PropertyInfo &prop : props) {
		if (prop.name != SNAME("actor_id") && prop.name != SNAME("marker_id")) {
			continue;
		}
		EditorInspectorPlugin *inspector = plugin.ptr();
		REQUIRE(inspector->can_handle(proxy));
		inspector->parse_begin(proxy);
		REQUIRE(inspector->parse_property(proxy, prop.type, prop.name, prop.hint, prop.hint_string, prop.usage));
		auto *picker = Object::cast_to<SandboxIdPickerProperty>(inspector->added_editors.back()->get().property_editor);
		REQUIRE(picker);
		picker->set_object_and_property(proxy, prop.name);
		tree->get_root()->add_child(picker);
		picker->connect(SNAME("property_changed"), Callable(changes.ptr(), SNAME("set_meta")).unbind(2));
		Ref<InputEventMouseButton> click;
		click.instantiate();
		click->set_button_index(MouseButton::LEFT);
		click->set_pressed(true);
		Node3D *anchor = prop.name == SNAME("actor_id") ? rig : marker;
		click->set_position(camera->unproject_position(anchor->get_global_position()));
		CHECK_FALSE(plugin->forward_3d_gui_input(camera, click));
		CHECK_FALSE(picker->is_armed());
		plugin->arm_picker(picker->get_instance_id());
		CHECK(picker->is_armed());
		CHECK(plugin->forward_3d_gui_input(camera, click));
		CHECK_FALSE(picker->is_armed());
		CHECK_FALSE(plugin->forward_3d_gui_input(camera, click));
		inspector->added_editors.clear();
		memdelete(picker);
	}
	CHECK(String(changes->get_meta("actor_id", "")) == "actor.pilot");
	CHECK(String(changes->get_meta("marker_id", "")) == "Dock");
	memdelete(proxy);
	tree->set_edited_scene_root(nullptr);
	memdelete(scene);
}

TEST_CASE("[SandboxEditor][Gate8] Strict audit traverses mission subresources and all three native timeline reference formats") {
	Ref<SandboxSchemaRegistry> schema;
	schema.instantiate();
	schema->register_state_key("load", Variant::FLOAT);
	schema->register_state_key("label", Variant::STRING);
	schema->register_actor_id("pilot");
	schema->register_marker_id("Dock");
	schema->register_event("arrived");
	Ref<CondStateRange> range;
	range.instantiate();
	range->set_key("label");
	Ref<ObjectiveData> objective;
	objective.instantiate();
	objective->set_complete_condition(range);
	Ref<MissionData> mission;
	mission.instantiate();
	TypedArray<ObjectiveData> objectives;
	objectives.push_back(objective);
	mission->set_objectives(objectives);
	PackedStringArray errors = SandboxExportPlugin::audit_resource(mission, schema);
	REQUIRE(errors.size() == 1);
	CHECK(errors[0].contains("objectives[0]/complete_condition/key"));
	range->set_key("load");
	CHECK(SandboxExportPlugin::audit_resource(mission, schema).is_empty());
	Ref<Animation> animation;
	animation.instantiate();
	int events = animation->add_track(Animation::TYPE_EVENT_BUS);
	animation->track_set_path(events, NodePath("EventBus:arrived"));
	animation->track_insert_key(events, 0, Dictionary());
	animation->track_insert_key(events, 1, "unknown_event");
	int state = animation->add_track(Animation::TYPE_WORLD_STATE);
	animation->track_set_path(state, NodePath("WorldState:load"));
	animation->track_insert_key(state, 0, 3.0);
	Dictionary key;
	key["state_key"] = "label";
	key["value"] = true;
	animation->track_insert_key(state, 1, key);
	int commands = animation->add_track(Animation::TYPE_ACTOR_COMMAND);
	Dictionary payload;
	payload["marker_id"] = "missing_marker";
	animation->actor_command_track_insert_key(commands, 0, "unknown_actor", "go", payload);
	errors = SandboxExportPlugin::audit_resource(animation, schema);
	REQUIRE(errors.size() == 4);
	CHECK(errors[0].contains("unknown_event"));
	CHECK(errors[1].contains("label"));
	CHECK(errors[2].contains("unknown_actor"));
	CHECK(errors[3].contains("missing_marker"));
}

} // namespace TestSandboxEditorSchema
