#include "sandbox_export_plugin.h"

#include "sandbox_inspector_plugin.h"

#include "core/config/project_settings.h"
#include "core/io/resource_loader.h"
#include "editor/file_system/editor_file_system.h"
#include "scene/resources/animation.h"
#include "scene/resources/packed_scene.h"

#include "modules/sandbox_narrative/mission_data.h"
#include "modules/sandbox_sim/sim_module.h"

namespace {

struct SchemaAudit {
	Ref<SandboxSchemaRegistry> schema;
	HashSet<ObjectID> resources;
	HashSet<const void *> active_containers;
	PackedStringArray errors;

	void check(const String &p_path, SandboxIdHint p_hint, const StringName &p_id) {
		auto status = p_hint.validate(schema, p_id);
		if (status == SandboxSchemaRegistry::VALIDATION_MISSING_KEY) {
			errors.push_back(vformat(TTR("%s: unregistered ID '%s'."), p_path, p_id));
		} else if (status == SandboxSchemaRegistry::VALIDATION_TYPE_MISMATCH) {
			errors.push_back(vformat(TTR("%s: ID '%s' requires type %s."), p_path, p_id, Variant::get_type_name(p_hint.expected)));
		}
	}

	void animation(const Ref<Animation> &p_anim, const String &p_path) {
		for (int track = 0; track < p_anim->get_track_count(); track++) {
			Animation::TrackType type = p_anim->track_get_type(track);
			if (type != Animation::TYPE_WORLD_STATE && type != Animation::TYPE_EVENT_BUS && type != Animation::TYPE_ACTOR_COMMAND) {
				continue;
			}
			NodePath path = p_anim->track_get_path(track);
			StringName fallback = path.get_subname_count() ? path.get_concatenated_subnames() : StringName();
			for (int key = 0; key < p_anim->track_get_key_count(track); key++) {
				String location = vformat("%s/tracks/%d/keys/%d", p_path, track, key);
				Variant value = p_anim->track_get_key_value(track, key);
				Dictionary fields;
				if (value.get_type() == Variant::DICTIONARY) {
					fields = value;
				}
				switch (type) {
					case Animation::TYPE_WORLD_STATE: {
						StringName id = path.get_subname_count() ? fallback : StringName(String(path));
						if (fields.has("state_key") && fields.has("value")) {
							id = fields["state_key"];
							value = fields["value"];
						}
						check(location, { SandboxIdHint::STATE, value.get_type() }, id);
					} break;
					case Animation::TYPE_EVENT_BUS: {
						StringName id = p_anim->event_bus_track_get_key_event_id(track, key);
						check(location, { SandboxIdHint::EVENT }, id == StringName() ? fallback : id);
					} break;
					case Animation::TYPE_ACTOR_COMMAND: {
						check(location, { SandboxIdHint::ACTOR }, fields.get("actor_id", fallback));
						Dictionary payload = fields.get("payload", Dictionary());
						if (payload.has("marker_id")) {
							check(location + "/payload/marker_id", { SandboxIdHint::MARKER }, payload["marker_id"]);
						}
					} break;
					default:
						break;
				}
			}
		}
	}

	void visit(const Variant &p_value, const String &p_path, int p_depth = 0) {
		if (p_depth > Variant::MAX_RECURSION_DEPTH) {
			errors.push_back(vformat(TTR("%s: resource nesting limit exceeded."), p_path));
			return;
		}
		if (p_value.get_type() == Variant::ARRAY) {
			Array array = p_value;
			if (!active_containers.has(array.id())) {
				active_containers.insert(array.id());
				for (int i = 0; i < array.size(); i++) {
					visit(array[i], vformat("%s[%d]", p_path, i), p_depth + 1);
				}
				active_containers.erase(array.id());
			}
			return;
		}
		if (p_value.get_type() == Variant::DICTIONARY) {
			Dictionary dict = p_value;
			if (!active_containers.has(dict.id())) {
				active_containers.insert(dict.id());
				for (const Variant &key : dict.get_key_list()) {
					visit(dict[key], p_path + "/" + String(key), p_depth + 1);
				}
				active_containers.erase(dict.id());
			}
			return;
		}
		if (p_value.get_type() != Variant::OBJECT) {
			return;
		}
		Ref<Resource> res = p_value;
		if (res.is_null() || resources.has(res->get_instance_id())) {
			return;
		}
		resources.insert(res->get_instance_id());
		Ref<Animation> anim = res;
		if (anim.is_valid()) {
			animation(anim, p_path);
			return;
		}
		List<PropertyInfo> properties;
		res->get_property_list(&properties);
		for (const PropertyInfo &prop : properties) {
			if (!(prop.usage & PROPERTY_USAGE_STORAGE) || prop.name == SNAME("_editor_graph_meta") || prop.name == SNAME("script")) {
				continue;
			}
			SandboxIdHint hint = SandboxIdHint::parse(prop.hint_string);
			const String location = p_path + "/" + prop.name;
			if (hint.kind != SandboxIdHint::NONE && (prop.type == Variant::STRING || prop.type == Variant::STRING_NAME)) {
				StringName id = res->get(prop.name);
				// An animation can target a local player instead of a registered actor.
				if (id == StringName() && Object::cast_to<ActionPlayAnimation>(res.ptr()) && prop.name == SNAME("target_actor_id")) {
					continue;
				}
				check(location, hint, id);
			} else if (prop.type == Variant::OBJECT || prop.type == Variant::ARRAY || prop.type == Variant::DICTIONARY || prop.type == Variant::NIL) {
				visit(res->get(prop.name), location, p_depth + 1);
			}
		}
	}
};

struct SchemaFiles {
	PackedStringArray authored;
	PackedStringArray scenes;
	PackedStringArray modules;

	void collect(EditorFileSystemDirectory *p_dir) {
		for (int i = 0; i < p_dir->get_file_count(); i++) {
			String path = p_dir->get_file_path(i);
			StringName type = p_dir->get_file_type(i);
			if (ClassDB::is_parent_class(type, SNAME("PackedScene"))) {
				scenes.push_back(path);
			} else if (ClassDB::is_parent_class(type, SNAME("SimModule"))) {
				modules.push_back(path);
			} else if (ClassDB::is_parent_class(type, SNAME("MissionData")) || ClassDB::is_parent_class(type, SNAME("SequenceData")) || ClassDB::is_parent_class(type, SNAME("Animation"))) {
				authored.push_back(path);
			}
		}
		for (int i = 0; i < p_dir->get_subdir_count(); i++) {
			collect(p_dir->get_subdir(i));
		}
	}
};

} // namespace

PackedStringArray SandboxExportPlugin::audit_resource(const Ref<Resource> &p_resource, const Ref<SandboxSchemaRegistry> &p_schema) {
	SchemaAudit audit;
	audit.schema = p_schema;
	audit.visit(p_resource, p_resource->get_path().is_empty() ? p_resource->get_class() : p_resource->get_path());
	return audit.errors;
}

Error SandboxExportPlugin::_export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) {
	if (!bool(GLOBAL_GET("sandbox/schema/strict_mode"))) {
		return OK;
	}
	Ref<EditorExportPlatform> platform = get_export_platform();
	EditorFileSystem *fs = EditorFileSystem::get_singleton();
	if (!fs || fs->is_scanning() || !fs->get_filesystem()) {
		platform->add_message(EditorExportPlatform::EXPORT_MESSAGE_ERROR, TTR("Sandbox Schema"), TTR("Finish scanning project resources before exporting in strict mode."));
		return ERR_BUSY;
	}
	String registry_path = GLOBAL_GET("sandbox/schema/registry_path");
	Ref<SandboxSchemaRegistry> schema;
	if (!registry_path.is_empty()) {
		schema = ResourceLoader::load(registry_path, "SandboxSchemaRegistry", ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP);
		if (schema.is_null()) {
			platform->add_message(EditorExportPlatform::EXPORT_MESSAGE_ERROR, TTR("Sandbox Schema"), vformat(TTR("Could not load schema registry %s."), registry_path));
			return ERR_INVALID_DATA;
		}
	} else {
		schema.instantiate();
	}
	SchemaFiles files;
	files.collect(fs->get_filesystem());
	PackedStringArray errors;
	for (const String &path : files.modules) {
		Ref<SimModule> module = ResourceLoader::load(path, "", ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP);
		if (module.is_valid()) {
			schema->merge_module_schema(module->get_declared_schema());
		} else {
			errors.push_back(path);
		}
	}
	// Read saved scene declarations without instantiating nodes or running their lifecycle.
	for (const String &path : files.scenes) {
		Ref<PackedScene> scene = ResourceLoader::load(path, "PackedScene", ResourceFormatLoader::CACHE_MODE_IGNORE);
		if (scene.is_null()) {
			errors.push_back(path);
			continue;
		}
		Ref<SceneState> state = scene->get_state();
		for (int i = 0; i < state->get_node_count(); i++) {
			StringName type = state->get_node_type(i);
			if (ClassDB::is_parent_class(type, SNAME("Marker3D"))) {
				schema->register_marker_id(state->get_node_name(i));
			} else if (ClassDB::is_parent_class(type, SNAME("SandboxActor"))) {
				for (int prop = 0; prop < state->get_node_property_count(i); prop++) {
					if (state->get_node_property_name(i, prop) == SNAME("actor_id")) {
						StringName id = state->get_node_property_value(i, prop);
						if (id != StringName()) {
							schema->register_actor_id(id);
						}
					}
				}
			}
		}
	}
	for (const String &path : files.authored) {
		Ref<Resource> resource = ResourceLoader::load(path, "", ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP);
		if (resource.is_valid()) {
			errors.append_array(audit_resource(resource, schema));
		} else {
			errors.push_back(vformat(TTR("Could not audit %s."), path));
		}
	}
	for (const String &error : errors) {
		platform->add_message(EditorExportPlatform::EXPORT_MESSAGE_ERROR, TTR("Sandbox Schema"), error);
	}
	return errors.is_empty() ? OK : ERR_INVALID_DATA;
}

bool SandboxExportPlugin::_begin_customize_resources(const Ref<EditorExportPlatform> &p_platform, const Vector<String> &p_features) {
	return p_features.has("template_release");
}

Ref<Resource> SandboxExportPlugin::_customize_resource(const Ref<Resource> &p_resource, const String &p_path) {
	if (!Object::cast_to<MissionData>(p_resource.ptr()) && !Object::cast_to<SequenceData>(p_resource.ptr())) {
		return Ref<Resource>();
	}
	if (p_path.is_empty() && p_resource->get_path().is_resource_file()) {
		return Ref<Resource>(); // External dependencies are customized at their own export paths.
	}
	Dictionary meta = p_resource->get(SNAME("_editor_graph_meta"));
	if (meta.is_empty()) {
		return Ref<Resource>();
	}
	// The exporter visits embedded resources individually. Keep references to shared
	// actions and external sequences intact, and only replace this resource's layout.
	Ref<Resource> stripped = p_resource->duplicate(false);
	if (stripped.is_valid()) {
		stripped->set(SNAME("_editor_graph_meta"), Dictionary());
	}
	return stripped;
}
