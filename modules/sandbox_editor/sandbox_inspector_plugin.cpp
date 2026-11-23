#include "sandbox_inspector_plugin.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/callable_mp.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/scene/3d/node_3d_editor_viewport.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/marker_3d.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/popup.h"
#include "scene/gui/tree.h"
#include "scene/main/scene_tree.h"

#include "modules/sandbox_narrative/sandbox_actor.h"
#include "modules/sandbox_sim/sim_module.h"

SandboxIdHint SandboxIdHint::parse(const String &p_hint) {
	if (p_hint == "sandbox_event_id") {
		return { EVENT };
	}
	if (p_hint == "sandbox_actor_id") {
		return { ACTOR };
	}
	if (p_hint == "sandbox_marker_id") {
		return { MARKER };
	}
	if (p_hint == "sandbox_world_state_key") {
		return { STATE };
	}
	if (p_hint.begins_with("sandbox_world_state_key:")) {
		const String type = p_hint.trim_prefix("sandbox_world_state_key:");
		for (int i = 1; i < Variant::VARIANT_MAX; i++) {
			if (type.nocasecmp_to(Variant::get_type_name(Variant::Type(i))) == 0) {
				return { STATE, Variant::Type(i) };
			}
		}
	}
	return {};
}

SandboxSchemaRegistry::ValidationStatus SandboxIdHint::validate(const Ref<SandboxSchemaRegistry> &p_schema, const StringName &p_id) const {
	switch (kind) {
		case STATE:
			return p_schema->validate_state_key(p_id, expected);
		case EVENT:
			return p_schema->validate_event_id(p_id);
		case ACTOR:
			return p_schema->validate_actor_id(p_id);
		case MARKER:
			return p_schema->validate_marker_id(p_id);
		default:
			return SandboxSchemaRegistry::VALIDATION_MISSING_KEY;
	}
}

PackedStringArray SandboxIdHint::search(const Ref<SandboxSchemaRegistry> &p_schema, const String &p_filter) const {
	PackedStringArray ids;
	switch (kind) {
		case STATE:
			ids = p_schema->get_state_keys();
			break;
		case EVENT:
			ids = p_schema->get_event_ids();
			break;
		case ACTOR:
			ids = p_schema->get_actor_ids();
			break;
		case MARKER:
			ids = p_schema->get_marker_ids();
			break;
		default:
			break;
	}
	PackedStringArray matches;
	PackedStringArray incompatible;
	for (const String &id : ids) {
		if (!p_filter.is_subsequence_ofn(id)) {
			continue;
		}
		if (validate(p_schema, id) == SandboxSchemaRegistry::VALIDATION_TYPE_MISMATCH) {
			incompatible.push_back(id);
		} else {
			matches.push_back(id);
		}
	}
	matches.append_array(incompatible);
	return matches;
}

static Node *edited_root() {
	return SceneTree::get_singleton() ? SceneTree::get_singleton()->get_edited_scene_root() : nullptr;
}

Ref<SandboxSchemaRegistry> SandboxInspectorPlugin::discover_schema(Node *p_scene_root) {
	String path = GLOBAL_GET("sandbox/schema/registry_path");
	Ref<SandboxSchemaRegistry> manifest;
	if (!path.is_empty() && ResourceLoader::exists(path)) {
		manifest = ResourceLoader::load(path);
	} else if (path.is_empty()) {
		manifest = SandboxSchemaRegistry::get_active_registry();
	}
	Ref<SandboxSchemaRegistry> discovered;
	if (manifest.is_valid()) {
		discovered = manifest->duplicate();
	} else {
		discovered.instantiate();
	}
	List<Ref<Resource>> resources;
	ResourceCache::get_cached_resources(&resources);
	for (const Ref<Resource> &resource : resources) {
		Ref<SimModule> module = resource;
		if (module.is_valid()) {
			discovered->merge_module_schema(module->get_declared_schema());
		}
	}
	discovered->merge_scene_nodes(p_scene_root);
	return discovered;
}

bool SandboxInspectorPlugin::can_handle(Object *p_object) {
	// Timeline keyframe proxies also opt in through their property hints.
	return true;
}

void SandboxInspectorPlugin::parse_begin(Object *p_object) {
	schema.unref();
}

bool SandboxInspectorPlugin::parse_property(Object *p_object, Variant::Type p_type, const String &p_path, PropertyHint p_hint, const String &p_hint_text, BitField<PropertyUsageFlags> p_usage, bool p_wide) {
	if (p_type != Variant::STRING_NAME && p_type != Variant::STRING) {
		return false;
	}
	SandboxIdHint hint = SandboxIdHint::parse(p_hint_text);
	if (hint.kind == SandboxIdHint::NONE) {
		return false;
	}
	if (schema.is_null()) {
		schema = discover_schema(edited_root());
	}
	SandboxIdPickerProperty *picker = memnew(SandboxIdPickerProperty);
	picker->setup(this, hint, schema, p_type);
	add_property_editor(p_path, picker);
	return true;
}

static String validation_message(SandboxSchemaRegistry::ValidationStatus p_status) {
	switch (p_status) {
		case SandboxSchemaRegistry::VALIDATION_MISSING_KEY:
			return TTR("Unregistered ID");
		case SandboxSchemaRegistry::VALIDATION_DEPRECATED:
			return TTR("Deprecated ID");
		case SandboxSchemaRegistry::VALIDATION_TYPE_MISMATCH:
			return TTR("Incompatible state key type");
		default:
			return String();
	}
}

void SandboxIdPickerProperty::update_property() {
	if (schema.is_null() || !get_edited_object()) {
		return;
	}
	String id = get_edited_property_value();
	field->set_text(id.is_empty() ? TTR("Select ID") : id);
	auto result = hint.validate(schema, id);
	String warning = validation_message(result);
	field->set_tooltip_text(warning);
	const bool invalid = result == SandboxSchemaRegistry::VALIDATION_TYPE_MISMATCH || (result == SandboxSchemaRegistry::VALIDATION_MISSING_KEY && bool(GLOBAL_GET("sandbox/schema/strict_mode")));
	field->set_button_icon(warning.is_empty() ? Ref<Texture2D>() : get_editor_theme_icon(invalid ? SNAME("StatusError") : SNAME("StatusWarning")));
}

void SandboxIdPickerProperty::_open() {
	schema = SandboxInspectorPlugin::discover_schema(edited_root());
	const String current = get_edited_property_value();
	const String query = hint.validate(schema, current) == SandboxSchemaRegistry::VALIDATION_MISSING_KEY ? current : String();
	filter->set_text(query);
	filter->select_all();
	_filter(query);
	Rect2 rect = field->get_global_rect();
	popup->popup_on_parent(Rect2i(rect.position + Vector2(0, rect.size.y), Size2(360, 320) * EDSCALE));
	filter->grab_focus();
}

void SandboxIdPickerProperty::_filter(const String &p_text) {
	choices->clear();
	TreeItem *root = choices->create_item();
	HashMap<String, TreeItem *> groups;
	for (const String &id : hint.search(schema, p_text)) {
		TreeItem *parent = root;
		PackedStringArray parts = id.split(".");
		String prefix;
		for (int i = 0; i < parts.size() - 1; i++) {
			prefix = prefix.is_empty() ? parts[i] : prefix + "." + parts[i];
			if (!groups.has(prefix)) {
				TreeItem *group = choices->create_item(parent);
				group->set_text(0, parts[i]);
				group->set_selectable(0, false);
				group->set_selectable(1, false);
				groups.insert(prefix, group);
			}
			parent = groups[prefix];
		}
		TreeItem *item = choices->create_item(parent);
		item->set_text(0, parts[parts.size() - 1]);
		item->set_metadata(0, id);
		Dictionary desc;
		Variant::Type type = Variant::STRING_NAME;
		if (hint.kind == SandboxIdHint::STATE) {
			desc = schema->get_state_key_descriptor(id);
			type = Variant::Type(int(desc["value_type"]));
		} else if (hint.kind == SandboxIdHint::EVENT) {
			desc = schema->get_event_descriptor(id);
			type = Variant::DICTIONARY;
		}
		item->set_text(1, Variant::get_type_name(type));
		item->set_selectable(1, false);
		item->set_tooltip_text(0, (id + "\n" + String(desc.get("description", "")) + "\n" + validation_message(hint.validate(schema, id))).strip_edges());
	}
	status->set_text("");
	const bool strict = GLOBAL_GET("sandbox/schema/strict_mode");
	add->set_visible(!strict && !p_text.strip_edges().is_empty() && hint.validate(schema, p_text.strip_edges()) == SandboxSchemaRegistry::VALIDATION_MISSING_KEY);
}

void SandboxIdPickerProperty::_choose() {
	TreeItem *item = choices->get_selected();
	if (item && item->get_metadata(0).get_type() == Variant::STRING) {
		_submit(item->get_metadata(0));
	}
}

void SandboxIdPickerProperty::_submit(const String &p_id) {
	if (is_read_only() || !get_edited_object()) {
		return;
	}
	const String id = p_id.strip_edges();
	auto result = hint.validate(schema, id);
	if (bool(GLOBAL_GET("sandbox/schema/strict_mode")) && result != SandboxSchemaRegistry::VALIDATION_OK && result != SandboxSchemaRegistry::VALIDATION_DEPRECATED) {
		status->set_text(validation_message(result));
		return;
	}
	const Variant value = property_type == Variant::STRING ? Variant(id) : Variant(StringName(id));
	emit_changed(get_edited_property(), value);
	popup->hide();
}

void SandboxIdPickerProperty::_add_to_schema() {
	if (is_read_only() || bool(GLOBAL_GET("sandbox/schema/strict_mode"))) {
		return;
	}
	const String id = filter->get_text().strip_edges();
	if (id.is_empty()) {
		return;
	}
	String path = GLOBAL_GET("sandbox/schema/registry_path");
	const bool configure = path.is_empty();
	Ref<SandboxSchemaRegistry> manifest;
	if (configure) {
		path = "res://sandbox_schema.tres";
		manifest = SandboxSchemaRegistry::get_active_registry()->duplicate();
		if (FileAccess::exists(path)) {
			status->set_text(TTR("Choose a registry in Project Settings before adding an ID."));
			return;
		}
	} else if (ResourceLoader::exists(path)) {
		manifest = ResourceLoader::load(path);
	} else {
		manifest.instantiate();
	}
	if (manifest.is_null()) {
		status->set_text(TTR("The configured file is not a SandboxSchemaRegistry."));
		return;
	}
	// Stage the addition so a failed save cannot change the live registry.
	Ref<SandboxSchemaRegistry> updated = manifest->duplicate();
	switch (hint.kind) {
		case SandboxIdHint::STATE:
			updated->register_state_key(id, hint.expected == Variant::NIL ? Variant::FLOAT : hint.expected);
			break;
		case SandboxIdHint::EVENT:
			updated->register_event(id);
			break;
		case SandboxIdHint::ACTOR:
			updated->register_actor_id(id);
			break;
		case SandboxIdHint::MARKER:
			updated->register_marker_id(id);
			break;
		default:
			return;
	}
	Error err = ResourceSaver::save(updated, path);
	if (err == OK && configure) {
		ProjectSettings::get_singleton()->set_setting("sandbox/schema/registry_path", path);
		err = ProjectSettings::get_singleton()->save();
		if (err != OK) {
			ProjectSettings::get_singleton()->set_setting("sandbox/schema/registry_path", "");
		}
	}
	if (err != OK) {
		status->set_text(vformat(TTR("Could not save schema: %s"), error_names[err]));
		return;
	}
	manifest = ResourceLoader::load(path, "SandboxSchemaRegistry", ResourceFormatLoader::CACHE_MODE_REPLACE);
	if (manifest.is_null()) {
		status->set_text(vformat(TTR("Could not reload schema %s."), path));
		return;
	}
	SandboxSchemaRegistry::set_active_registry(manifest);
	manifest->emit_changed();
	schema = SandboxInspectorPlugin::discover_schema(edited_root());
	_submit(id);
}

struct SceneTarget {
	StringName id;
	Node3D *spatial = nullptr;
	Node *node = nullptr;
};

static void collect_targets(Node *p_node, SandboxIdHint::Kind p_kind, Vector<SceneTarget> &r_targets) {
	if (!p_node) {
		return;
	}
	if (p_kind == SandboxIdHint::ACTOR) {
		SandboxActor *actor = Object::cast_to<SandboxActor>(p_node);
		if (actor && actor->get_actor_id() != StringName()) {
			for (Node *parent = actor->get_parent(); parent; parent = parent->get_parent()) {
				Node3D *spatial = Object::cast_to<Node3D>(parent);
				if (spatial) {
					r_targets.push_back({ actor->get_actor_id(), spatial, actor });
					break;
				}
			}
		}
	} else if (Marker3D *marker = Object::cast_to<Marker3D>(p_node)) {
		r_targets.push_back({ marker->get_name(), marker, marker });
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		collect_targets(p_node->get_child(i), p_kind, r_targets);
	}
}

void SandboxIdPickerProperty::_focus() {
	Vector<SceneTarget> targets;
	collect_targets(edited_root(), hint.kind, targets);
	StringName id = get_edited_property_value();
	for (const SceneTarget &target : targets) {
		if (target.id == id) {
			EditorSelection *selection = EditorInterface::get_singleton()->get_selection();
			Node3DEditor::get_singleton()->make_visible();
			selection->clear();
			selection->add_node(target.spatial);
			selection->update();
			Node3DEditor::get_singleton()->get_last_used_viewport()->focus_selection();
			if (target.node != target.spatial) {
				selection->clear();
				selection->add_node(target.node);
				selection->update();
			}
			return;
		}
	}
}

bool SandboxIdPickerProperty::pick_at(Node *p_root, Camera3D *p_camera, const Point2 &p_position) {
	if (!p_camera || is_read_only()) {
		return false;
	}
	Vector<SceneTarget> targets;
	collect_targets(p_root, hint.kind, targets);
	// Actors are adapters without colliders; pick their spatial anchor like a marker gizmo.
	real_t best = Math::pow(16 * EDSCALE, 2);
	real_t best_depth = INFINITY;
	StringName id;
	for (const SceneTarget &target : targets) {
		Vector3 pos = target.spatial->get_global_position();
		if (p_camera->is_position_behind(pos) || !target.spatial->is_visible_in_tree()) {
			continue;
		}
		real_t distance = p_camera->unproject_position(pos).distance_squared_to(p_position);
		real_t depth = pos.distance_squared_to(p_camera->get_global_position());
		if (distance < best || (Math::is_equal_approx(distance, best) && depth < best_depth)) {
			best = distance;
			best_depth = depth;
			id = target.id;
		}
	}
	if (id == StringName()) {
		return false;
	}
	schema = SandboxInspectorPlugin::discover_schema(p_root);
	_submit(id);
	return true;
}

void SandboxInspectorPlugin::arm_picker(ObjectID p_picker) {
	armed_picker = p_picker;
	if (Node3DEditor::get_singleton()) {
		Node3DEditor::get_singleton()->make_visible();
	}
}

bool SandboxInspectorPlugin::forward_3d_gui_input(Camera3D *p_camera, const Ref<InputEvent> &p_event) {
	SandboxIdPickerProperty *picker = ObjectDB::get_instance<SandboxIdPickerProperty>(armed_picker);
	if (!picker || !picker->is_visible_in_tree() || picker->is_read_only()) {
		armed_picker = ObjectID();
		return false;
	}
	if (p_event->is_action_pressed(SNAME("ui_cancel"))) {
		armed_picker = ObjectID();
		return true;
	}
	Ref<InputEventMouseButton> mouse = p_event;
	if (mouse.is_null() || !mouse->is_pressed() || mouse->get_button_index() != MouseButton::LEFT) {
		return false;
	}
	armed_picker = ObjectID();
	picker->pick_at(edited_root(), p_camera, mouse->get_position());
	return true;
}

void SandboxIdPickerProperty::_set_read_only(bool p_read_only) {
	field->set_disabled(p_read_only);
	pick->set_disabled(p_read_only);
	if (p_read_only) {
		popup->hide();
	}
}

void SandboxIdPickerProperty::setup(SandboxInspectorPlugin *p_plugin, SandboxIdHint p_hint, const Ref<SandboxSchemaRegistry> &p_schema, Variant::Type p_property_type) {
	hint = p_hint;
	schema = p_schema;
	property_type = p_property_type;
	const bool spatial = hint.kind == SandboxIdHint::ACTOR || hint.kind == SandboxIdHint::MARKER;
	pick->set_visible(spatial);
	focus->set_visible(spatial);
	pick->connect(SceneStringName(pressed), callable_mp(p_plugin, &SandboxInspectorPlugin::arm_picker).bind(get_instance_id()));
}

void SandboxIdPickerProperty::_notification(int p_what) {
	if (p_what == NOTIFICATION_THEME_CHANGED) {
		pick->set_button_icon(get_editor_theme_icon(SNAME("ColorPick")));
		focus->set_button_icon(get_editor_theme_icon(SNAME("CenterView")));
	}
}

SandboxIdPickerProperty::SandboxIdPickerProperty() {
	HBoxContainer *row = memnew(HBoxContainer);
	add_child(row);
	field = memnew(Button);
	row->add_child(field);
	field->set_h_size_flags(SIZE_EXPAND_FILL);
	field->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	add_focusable(field);
	field->connect(SceneStringName(pressed), callable_mp(this, &SandboxIdPickerProperty::_open));
	pick = memnew(Button);
	row->add_child(pick);
	pick->set_tooltip_text(TTR("Pick the actor or marker at its origin in the 3D viewport."));
	focus = memnew(Button);
	row->add_child(focus);
	focus->set_tooltip_text(TTR("Frame this ID in the 3D viewport."));
	focus->connect(SceneStringName(pressed), callable_mp(this, &SandboxIdPickerProperty::_focus));

	popup = memnew(PopupPanel);
	add_child(popup);
	VBoxContainer *column = memnew(VBoxContainer);
	popup->add_child(column);
	filter = memnew(LineEdit);
	filter->set_placeholder(TTR("Search IDs"));
	column->add_child(filter);
	filter->connect(SceneStringName(text_changed), callable_mp(this, &SandboxIdPickerProperty::_filter));
	filter->connect(SceneStringName(text_submitted), callable_mp(this, &SandboxIdPickerProperty::_submit));
	choices = memnew(Tree);
	column->add_child(choices);
	choices->set_hide_root(true);
	choices->set_columns(2);
	choices->set_v_size_flags(SIZE_EXPAND_FILL);
	choices->connect(SNAME("item_activated"), callable_mp(this, &SandboxIdPickerProperty::_choose));
	status = memnew(Label);
	column->add_child(status);
	add = memnew(Button);
	add->set_text(TTR("Add to Schema Registry"));
	column->add_child(add);
	add->connect(SceneStringName(pressed), callable_mp(this, &SandboxIdPickerProperty::_add_to_schema));
}
