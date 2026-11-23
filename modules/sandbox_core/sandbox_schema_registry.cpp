/**************************************************************************/
/*  sandbox_schema_registry.cpp                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "sandbox_schema_registry.h"

#include "core/config/project_settings.h"
#include "core/io/resource_loader.h"
#include "scene/main/node.h"

Ref<SandboxSchemaRegistry> SandboxSchemaRegistry::get_active_registry() {
	if (active_registry.is_valid()) {
		return active_registry;
	}
	if (ProjectSettings::get_singleton() && ProjectSettings::get_singleton()->has_setting("sandbox/schema/registry_path")) {
		String path = GLOBAL_GET("sandbox/schema/registry_path");
		if (!path.is_empty() && ResourceLoader::exists(path)) {
			Ref<SandboxSchemaRegistry> loaded = ResourceLoader::load(path);
			if (loaded.is_valid()) {
				active_registry = loaded;
				return active_registry;
			}
		}
	}
	active_registry.instantiate();
	return active_registry;
}

void SandboxSchemaRegistry::set_active_registry(const Ref<SandboxSchemaRegistry> &p_registry) {
	active_registry = p_registry;
}

void SandboxSchemaRegistry::cleanup_active_registry() {
	active_registry.unref();
}

void SandboxSchemaRegistry::register_state_key(const StringName &p_key, Variant::Type p_value_type, const Variant &p_default_value, const Vector2 &p_range_hint, bool p_deprecated, const String &p_description, bool p_dynamic) {
	ERR_FAIL_COND(p_key == StringName());
	StateKeyDescriptor desc;
	desc.key = p_key;
	desc.value_type = p_value_type;
	desc.default_value = p_default_value;
	desc.range_hint = p_range_hint;
	desc.deprecated = p_deprecated;
	desc.description = p_description;
	desc.dynamically_discovered = p_dynamic;
	state_keys[p_key] = desc;
}

void SandboxSchemaRegistry::register_event(const StringName &p_event_id, const Dictionary &p_payload_schema, bool p_deprecated, const String &p_description, bool p_dynamic) {
	ERR_FAIL_COND(p_event_id == StringName());
	EventDescriptor desc;
	desc.event_id = p_event_id;
	desc.payload_schema = p_payload_schema;
	desc.deprecated = p_deprecated;
	desc.description = p_description;
	desc.dynamically_discovered = p_dynamic;
	events[p_event_id] = desc;
}

void SandboxSchemaRegistry::register_actor_id(const StringName &p_actor_id, const String &p_description) {
	ERR_FAIL_COND(p_actor_id == StringName());
	actor_catalog[p_actor_id] = p_description;
}

void SandboxSchemaRegistry::register_tag(const StringName &p_tag, const String &p_description) {
	ERR_FAIL_COND(p_tag == StringName());
	tag_catalog[p_tag] = p_description;
}

void SandboxSchemaRegistry::register_marker_id(const StringName &p_marker_id, const String &p_description) {
	ERR_FAIL_COND(p_marker_id == StringName());
	marker_catalog[p_marker_id] = p_description;
}

void SandboxSchemaRegistry::register_archetype_id(const StringName &p_archetype_id, const String &p_description) {
	ERR_FAIL_COND(p_archetype_id == StringName());
	archetype_catalog[p_archetype_id] = p_description;
}

Dictionary SandboxSchemaRegistry::get_state_key_descriptor(const StringName &p_key) const {
	Dictionary d;
	HashMap<StringName, StateKeyDescriptor>::ConstIterator it = state_keys.find(p_key);
	if (!it) {
		return d;
	}
	d["key"] = it->value.key;
	d["value_type"] = (int)it->value.value_type;
	d["default_value"] = it->value.default_value;
	d["range_hint"] = it->value.range_hint;
	d["deprecated"] = it->value.deprecated;
	d["description"] = it->value.description;
	d["dynamically_discovered"] = it->value.dynamically_discovered;
	return d;
}

Dictionary SandboxSchemaRegistry::get_event_descriptor(const StringName &p_event_id) const {
	Dictionary d;
	HashMap<StringName, EventDescriptor>::ConstIterator it = events.find(p_event_id);
	if (!it) {
		return d;
	}
	d["event_id"] = it->value.event_id;
	d["payload_schema"] = it->value.payload_schema;
	d["deprecated"] = it->value.deprecated;
	d["description"] = it->value.description;
	d["dynamically_discovered"] = it->value.dynamically_discovered;
	return d;
}

PackedStringArray SandboxSchemaRegistry::get_state_keys(Variant::Type p_filter_type) const {
	PackedStringArray out;
	for (const KeyValue<StringName, StateKeyDescriptor> &E : state_keys) {
		if (p_filter_type != Variant::NIL && E.value.value_type != p_filter_type) {
			// Allow INT keys to appear when filtering for FLOAT numeric keys.
			if (!(p_filter_type == Variant::FLOAT && E.value.value_type == Variant::INT)) {
				continue;
			}
		}
		out.push_back(String(E.key));
	}
	out.sort();
	return out;
}

PackedStringArray SandboxSchemaRegistry::get_event_ids() const {
	PackedStringArray out;
	for (const KeyValue<StringName, EventDescriptor> &E : events) {
		out.push_back(String(E.key));
	}
	out.sort();
	return out;
}

PackedStringArray SandboxSchemaRegistry::get_actor_ids() const {
	PackedStringArray out;
	for (const KeyValue<StringName, String> &E : actor_catalog) {
		out.push_back(String(E.key));
	}
	out.sort();
	return out;
}

PackedStringArray SandboxSchemaRegistry::get_tags() const {
	PackedStringArray out;
	for (const KeyValue<StringName, String> &E : tag_catalog) {
		out.push_back(String(E.key));
	}
	out.sort();
	return out;
}

PackedStringArray SandboxSchemaRegistry::get_marker_ids() const {
	PackedStringArray out;
	for (const KeyValue<StringName, String> &E : marker_catalog) {
		out.push_back(String(E.key));
	}
	out.sort();
	return out;
}

PackedStringArray SandboxSchemaRegistry::get_archetype_ids() const {
	PackedStringArray out;
	for (const KeyValue<StringName, String> &E : archetype_catalog) {
		out.push_back(String(E.key));
	}
	out.sort();
	return out;
}

SandboxSchemaRegistry::ValidationStatus SandboxSchemaRegistry::validate_state_key(const StringName &p_key, Variant::Type p_expected_type) const {
	HashMap<StringName, StateKeyDescriptor>::ConstIterator it = state_keys.find(p_key);
	if (!it) {
		return VALIDATION_MISSING_KEY;
	}
	if (p_expected_type != Variant::NIL && it->value.value_type != p_expected_type) {
		if (!(p_expected_type == Variant::FLOAT && it->value.value_type == Variant::INT)) {
			return VALIDATION_TYPE_MISMATCH;
		}
	}
	return it->value.deprecated ? VALIDATION_DEPRECATED : VALIDATION_OK;
}

SandboxSchemaRegistry::ValidationStatus SandboxSchemaRegistry::validate_event_id(const StringName &p_event_id) const {
	HashMap<StringName, EventDescriptor>::ConstIterator it = events.find(p_event_id);
	if (!it) {
		return VALIDATION_MISSING_KEY;
	}
	if (it->value.deprecated) {
		return VALIDATION_DEPRECATED;
	}
	return VALIDATION_OK;
}

SandboxSchemaRegistry::ValidationStatus SandboxSchemaRegistry::validate_actor_id(const StringName &p_actor_id) const {
	return actor_catalog.has(p_actor_id) ? VALIDATION_OK : VALIDATION_MISSING_KEY;
}

SandboxSchemaRegistry::ValidationStatus SandboxSchemaRegistry::validate_marker_id(const StringName &p_marker_id) const {
	return marker_catalog.has(p_marker_id) ? VALIDATION_OK : VALIDATION_MISSING_KEY;
}

void SandboxSchemaRegistry::merge_module_schema(const Dictionary &p_schema) {
	if (p_schema.has("state_keys")) {
		Array keys = p_schema["state_keys"];
		for (int i = 0; i < keys.size(); i++) {
			Dictionary kd = keys[i];
			StringName key = kd.get("key", StringName());
			if (key != StringName() && !state_keys.has(key)) {
				Variant::Type vt = (Variant::Type)(int)kd.get("value_type", (int)Variant::FLOAT);
				register_state_key(
						key,
						vt,
						kd.get("default_value", Variant()),
						kd.get("range_hint", Vector2()),
						(bool)kd.get("deprecated", false),
						kd.get("description", String()),
						true);
			}
		}
	}
	if (p_schema.has("events")) {
		Array evs = p_schema["events"];
		for (int i = 0; i < evs.size(); i++) {
			Dictionary ed = evs[i];
			StringName ev_id = ed.get("event_id", StringName());
			if (ev_id != StringName() && !events.has(ev_id)) {
				register_event(
						ev_id,
						ed.get("payload_schema", Dictionary()),
						(bool)ed.get("deprecated", false),
						ed.get("description", String()),
						true);
			}
		}
	}
}

void SandboxSchemaRegistry::_scan_scene_node_recursive(Node *p_node) {
	if (!p_node) {
		return;
	}
	bool is_valid_prop = false;
	Variant actor_id_var;
	if (p_node->is_class("SandboxActor")) {
		actor_id_var = p_node->get(SNAME("actor_id"), &is_valid_prop);
	}
	if (is_valid_prop && (actor_id_var.get_type() == Variant::STRING_NAME || actor_id_var.get_type() == Variant::STRING)) {
		StringName aid(actor_id_var.operator String());
		if (aid != StringName()) {
			register_actor_id(aid, "Discovered from scene node " + String(p_node->get_name()));
		}
	}
	if (p_node->is_class("Marker3D")) {
		register_marker_id(p_node->get_name(), "Discovered Marker3D in scene");
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_scan_scene_node_recursive(p_node->get_child(i));
	}
}

void SandboxSchemaRegistry::merge_scene_nodes(Node *p_scene_root) {
	_scan_scene_node_recursive(p_scene_root);
}

void SandboxSchemaRegistry::set_state_keys_data(const TypedArray<Dictionary> &p_data) {
	state_keys.clear();
	for (int i = 0; i < p_data.size(); i++) {
		Dictionary d = p_data[i];
		StringName key = d.get("key", StringName());
		if (key != StringName()) {
			register_state_key(
					key,
					(Variant::Type)(int)d.get("value_type", (int)Variant::FLOAT),
					d.get("default_value", Variant()),
					d.get("range_hint", Vector2()),
					(bool)d.get("deprecated", false),
					d.get("description", String()),
					false);
		}
	}
}

TypedArray<Dictionary> SandboxSchemaRegistry::get_state_keys_data() const {
	TypedArray<Dictionary> out;
	PackedStringArray sorted = get_state_keys();
	for (int i = 0; i < sorted.size(); i++) {
		StringName k(sorted[i]);
		const StateKeyDescriptor &desc = state_keys[k];
		if (desc.dynamically_discovered) {
			continue;
		}
		out.push_back(get_state_key_descriptor(k));
	}
	return out;
}

void SandboxSchemaRegistry::set_events_data(const TypedArray<Dictionary> &p_data) {
	events.clear();
	for (int i = 0; i < p_data.size(); i++) {
		Dictionary d = p_data[i];
		StringName ev = d.get("event_id", StringName());
		if (ev != StringName()) {
			register_event(
					ev,
					d.get("payload_schema", Dictionary()),
					(bool)d.get("deprecated", false),
					d.get("description", String()),
					false);
		}
	}
}

TypedArray<Dictionary> SandboxSchemaRegistry::get_events_data() const {
	TypedArray<Dictionary> out;
	PackedStringArray sorted = get_event_ids();
	for (int i = 0; i < sorted.size(); i++) {
		StringName k(sorted[i]);
		const EventDescriptor &desc = events[k];
		if (desc.dynamically_discovered) {
			continue;
		}
		out.push_back(get_event_descriptor(k));
	}
	return out;
}

void SandboxSchemaRegistry::set_actor_ids_data(const PackedStringArray &p_actors) {
	actor_catalog.clear();
	for (int i = 0; i < p_actors.size(); i++) {
		register_actor_id(StringName(p_actors[i]));
	}
}

PackedStringArray SandboxSchemaRegistry::get_actor_ids_data() const {
	return get_actor_ids();
}

void SandboxSchemaRegistry::set_tags_data(const PackedStringArray &p_tags) {
	tag_catalog.clear();
	for (int i = 0; i < p_tags.size(); i++) {
		register_tag(StringName(p_tags[i]));
	}
}

PackedStringArray SandboxSchemaRegistry::get_tags_data() const {
	return get_tags();
}

void SandboxSchemaRegistry::set_marker_ids_data(const PackedStringArray &p_markers) {
	marker_catalog.clear();
	for (int i = 0; i < p_markers.size(); i++) {
		register_marker_id(StringName(p_markers[i]));
	}
}

PackedStringArray SandboxSchemaRegistry::get_marker_ids_data() const {
	return get_marker_ids();
}

void SandboxSchemaRegistry::set_archetype_ids_data(const PackedStringArray &p_archetypes) {
	archetype_catalog.clear();
	for (int i = 0; i < p_archetypes.size(); i++) {
		register_archetype_id(StringName(p_archetypes[i]));
	}
}

PackedStringArray SandboxSchemaRegistry::get_archetype_ids_data() const {
	return get_archetype_ids();
}

void SandboxSchemaRegistry::clear() {
	state_keys.clear();
	events.clear();
	actor_catalog.clear();
	tag_catalog.clear();
	marker_catalog.clear();
	archetype_catalog.clear();
}

void SandboxSchemaRegistry::_bind_methods() {
	ClassDB::bind_static_method("SandboxSchemaRegistry", D_METHOD("get_active_registry"), &SandboxSchemaRegistry::get_active_registry);
	ClassDB::bind_static_method("SandboxSchemaRegistry", D_METHOD("set_active_registry", "registry"), &SandboxSchemaRegistry::set_active_registry);

	ClassDB::bind_method(D_METHOD("register_state_key", "key", "value_type", "default_value", "range_hint", "deprecated", "description", "dynamic"), &SandboxSchemaRegistry::register_state_key, DEFVAL(Variant::FLOAT), DEFVAL(Variant()), DEFVAL(Vector2()), DEFVAL(false), DEFVAL(String()), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("register_event", "event_id", "payload_schema", "deprecated", "description", "dynamic"), &SandboxSchemaRegistry::register_event, DEFVAL(Dictionary()), DEFVAL(false), DEFVAL(String()), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("register_actor_id", "actor_id", "description"), &SandboxSchemaRegistry::register_actor_id, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("register_tag", "tag", "description"), &SandboxSchemaRegistry::register_tag, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("register_marker_id", "marker_id", "description"), &SandboxSchemaRegistry::register_marker_id, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("register_archetype_id", "archetype_id", "description"), &SandboxSchemaRegistry::register_archetype_id, DEFVAL(String()));

	ClassDB::bind_method(D_METHOD("has_state_key", "key"), &SandboxSchemaRegistry::has_state_key);
	ClassDB::bind_method(D_METHOD("has_event", "event_id"), &SandboxSchemaRegistry::has_event);
	ClassDB::bind_method(D_METHOD("has_actor_id", "actor_id"), &SandboxSchemaRegistry::has_actor_id);
	ClassDB::bind_method(D_METHOD("has_tag", "tag"), &SandboxSchemaRegistry::has_tag);
	ClassDB::bind_method(D_METHOD("has_marker_id", "marker_id"), &SandboxSchemaRegistry::has_marker_id);
	ClassDB::bind_method(D_METHOD("has_archetype_id", "archetype_id"), &SandboxSchemaRegistry::has_archetype_id);

	ClassDB::bind_method(D_METHOD("get_state_key_descriptor", "key"), &SandboxSchemaRegistry::get_state_key_descriptor);
	ClassDB::bind_method(D_METHOD("get_event_descriptor", "event_id"), &SandboxSchemaRegistry::get_event_descriptor);

	ClassDB::bind_method(D_METHOD("get_state_keys", "filter_type"), &SandboxSchemaRegistry::get_state_keys, DEFVAL(Variant::NIL));
	ClassDB::bind_method(D_METHOD("get_event_ids"), &SandboxSchemaRegistry::get_event_ids);
	ClassDB::bind_method(D_METHOD("get_actor_ids"), &SandboxSchemaRegistry::get_actor_ids);
	ClassDB::bind_method(D_METHOD("get_tags"), &SandboxSchemaRegistry::get_tags);
	ClassDB::bind_method(D_METHOD("get_marker_ids"), &SandboxSchemaRegistry::get_marker_ids);
	ClassDB::bind_method(D_METHOD("get_archetype_ids"), &SandboxSchemaRegistry::get_archetype_ids);

	ClassDB::bind_method(D_METHOD("validate_state_key", "key", "expected_type"), &SandboxSchemaRegistry::validate_state_key, DEFVAL(Variant::NIL));
	ClassDB::bind_method(D_METHOD("validate_event_id", "event_id"), &SandboxSchemaRegistry::validate_event_id);
	ClassDB::bind_method(D_METHOD("validate_actor_id", "actor_id"), &SandboxSchemaRegistry::validate_actor_id);
	ClassDB::bind_method(D_METHOD("validate_marker_id", "marker_id"), &SandboxSchemaRegistry::validate_marker_id);

	ClassDB::bind_method(D_METHOD("merge_module_schema", "schema"), &SandboxSchemaRegistry::merge_module_schema);
	ClassDB::bind_method(D_METHOD("merge_scene_nodes", "scene_root"), &SandboxSchemaRegistry::merge_scene_nodes);

	ClassDB::bind_method(D_METHOD("set_state_keys_data", "data"), &SandboxSchemaRegistry::set_state_keys_data);
	ClassDB::bind_method(D_METHOD("get_state_keys_data"), &SandboxSchemaRegistry::get_state_keys_data);
	ClassDB::bind_method(D_METHOD("set_events_data", "data"), &SandboxSchemaRegistry::set_events_data);
	ClassDB::bind_method(D_METHOD("get_events_data"), &SandboxSchemaRegistry::get_events_data);
	ClassDB::bind_method(D_METHOD("set_actor_ids_data", "actors"), &SandboxSchemaRegistry::set_actor_ids_data);
	ClassDB::bind_method(D_METHOD("get_actor_ids_data"), &SandboxSchemaRegistry::get_actor_ids_data);
	ClassDB::bind_method(D_METHOD("set_tags_data", "tags"), &SandboxSchemaRegistry::set_tags_data);
	ClassDB::bind_method(D_METHOD("get_tags_data"), &SandboxSchemaRegistry::get_tags_data);
	ClassDB::bind_method(D_METHOD("set_marker_ids_data", "markers"), &SandboxSchemaRegistry::set_marker_ids_data);
	ClassDB::bind_method(D_METHOD("get_marker_ids_data"), &SandboxSchemaRegistry::get_marker_ids_data);
	ClassDB::bind_method(D_METHOD("set_archetype_ids_data", "archetypes"), &SandboxSchemaRegistry::set_archetype_ids_data);
	ClassDB::bind_method(D_METHOD("get_archetype_ids_data"), &SandboxSchemaRegistry::get_archetype_ids_data);
	ClassDB::bind_method(D_METHOD("clear"), &SandboxSchemaRegistry::clear);

	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "state_keys_data", PROPERTY_HINT_ARRAY_TYPE, "Dictionary"), "set_state_keys_data", "get_state_keys_data");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "events_data", PROPERTY_HINT_ARRAY_TYPE, "Dictionary"), "set_events_data", "get_events_data");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "actor_ids_data"), "set_actor_ids_data", "get_actor_ids_data");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "tags_data"), "set_tags_data", "get_tags_data");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "marker_ids_data"), "set_marker_ids_data", "get_marker_ids_data");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "archetype_ids_data"), "set_archetype_ids_data", "get_archetype_ids_data");

	BIND_ENUM_CONSTANT(VALIDATION_OK);
	BIND_ENUM_CONSTANT(VALIDATION_MISSING_KEY);
	BIND_ENUM_CONSTANT(VALIDATION_DEPRECATED);
	BIND_ENUM_CONSTANT(VALIDATION_TYPE_MISMATCH);
}
