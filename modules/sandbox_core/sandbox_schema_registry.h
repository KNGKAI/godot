/**************************************************************************/
/*  sandbox_schema_registry.h                                             */
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

#pragma once

#include "core/io/resource.h"
#include "core/math/vector2.h"
#include "core/object/class_db.h"
#include "core/templates/hash_map.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"

class Node;

class SandboxSchemaRegistry : public Resource {
	GDCLASS(SandboxSchemaRegistry, Resource);

public:
	enum ValidationStatus {
		VALIDATION_OK = 0,
		VALIDATION_MISSING_KEY = 1,
		VALIDATION_DEPRECATED = 2,
		VALIDATION_TYPE_MISMATCH = 3,
	};

	struct StateKeyDescriptor {
		StringName key;
		Variant::Type value_type = Variant::FLOAT;
		Variant default_value;
		Vector2 range_hint;
		bool deprecated = false;
		String description;
		bool dynamically_discovered = false;
	};

	struct EventDescriptor {
		StringName event_id;
		Dictionary payload_schema;
		bool deprecated = false;
		String description;
		bool dynamically_discovered = false;
	};

private:
	static inline Ref<SandboxSchemaRegistry> active_registry;

	HashMap<StringName, StateKeyDescriptor> state_keys;
	HashMap<StringName, EventDescriptor> events;
	HashMap<StringName, String> actor_catalog;
	HashMap<StringName, String> tag_catalog;
	HashMap<StringName, String> marker_catalog;
	HashMap<StringName, String> archetype_catalog;

	void _scan_scene_node_recursive(Node *p_node);

protected:
	static void _bind_methods();

public:
	static Ref<SandboxSchemaRegistry> get_active_registry();
	static void set_active_registry(const Ref<SandboxSchemaRegistry> &p_registry);
	static void cleanup_active_registry();

	// Registration API
	void register_state_key(const StringName &p_key, Variant::Type p_value_type = Variant::FLOAT, const Variant &p_default_value = Variant(), const Vector2 &p_range_hint = Vector2(), bool p_deprecated = false, const String &p_description = String(), bool p_dynamic = false);
	void register_event(const StringName &p_event_id, const Dictionary &p_payload_schema = Dictionary(), bool p_deprecated = false, const String &p_description = String(), bool p_dynamic = false);
	void register_actor_id(const StringName &p_actor_id, const String &p_description = String());
	void register_tag(const StringName &p_tag, const String &p_description = String());
	void register_marker_id(const StringName &p_marker_id, const String &p_description = String());
	void register_archetype_id(const StringName &p_archetype_id, const String &p_description = String());

	// Queries
	bool has_state_key(const StringName &p_key) const { return state_keys.has(p_key); }
	bool has_event(const StringName &p_event_id) const { return events.has(p_event_id); }
	bool has_actor_id(const StringName &p_actor_id) const { return actor_catalog.has(p_actor_id); }
	bool has_tag(const StringName &p_tag) const { return tag_catalog.has(p_tag); }
	bool has_marker_id(const StringName &p_marker_id) const { return marker_catalog.has(p_marker_id); }
	bool has_archetype_id(const StringName &p_archetype_id) const { return archetype_catalog.has(p_archetype_id); }

	Dictionary get_state_key_descriptor(const StringName &p_key) const;
	Dictionary get_event_descriptor(const StringName &p_event_id) const;

	PackedStringArray get_state_keys(Variant::Type p_filter_type = Variant::NIL) const;
	PackedStringArray get_event_ids() const;
	PackedStringArray get_actor_ids() const;
	PackedStringArray get_tags() const;
	PackedStringArray get_marker_ids() const;
	PackedStringArray get_archetype_ids() const;

	// Validation API (§3.7.3)
	ValidationStatus validate_state_key(const StringName &p_key, Variant::Type p_expected_type = Variant::NIL) const;
	ValidationStatus validate_event_id(const StringName &p_event_id) const;
	ValidationStatus validate_actor_id(const StringName &p_actor_id) const;
	ValidationStatus validate_marker_id(const StringName &p_marker_id) const;

	// Dynamic Discovery Merge (§3.7.3)
	void merge_module_schema(const Dictionary &p_schema);
	void merge_scene_nodes(Node *p_scene_root);

	// Resource Serialization Properties (.tres manifest persistence)
	void set_state_keys_data(const TypedArray<Dictionary> &p_data);
	TypedArray<Dictionary> get_state_keys_data() const;

	void set_events_data(const TypedArray<Dictionary> &p_data);
	TypedArray<Dictionary> get_events_data() const;

	void set_actor_ids_data(const PackedStringArray &p_actors);
	PackedStringArray get_actor_ids_data() const;

	void set_tags_data(const PackedStringArray &p_tags);
	PackedStringArray get_tags_data() const;

	void set_marker_ids_data(const PackedStringArray &p_markers);
	PackedStringArray get_marker_ids_data() const;

	void set_archetype_ids_data(const PackedStringArray &p_archetypes);
	PackedStringArray get_archetype_ids_data() const;

	void clear();
};

VARIANT_ENUM_CAST(SandboxSchemaRegistry::ValidationStatus);
