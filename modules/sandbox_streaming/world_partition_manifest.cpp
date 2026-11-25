/**************************************************************************/
/*  world_partition_manifest.cpp                                          */
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

#include "world_partition_manifest.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

namespace {

constexpr const char *bundle_extensions[] = { "tscn", "scn", "tres", "res", "mesh", "obj", "glb", "gltf", "fbx", "dae", "blend" };

bool is_valid_id(const StringName &p_id) {
	const String id = p_id;
	return !id.is_empty() && id == id.strip_edges();
}

} // namespace

void WorldPartitionLayer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_layer_id", "layer_id"), &WorldPartitionLayer::set_layer_id);
	ClassDB::bind_method(D_METHOD("get_layer_id"), &WorldPartitionLayer::get_layer_id);
	ClassDB::bind_method(D_METHOD("set_cell_size", "cell_size"), &WorldPartitionLayer::set_cell_size);
	ClassDB::bind_method(D_METHOD("get_cell_size"), &WorldPartitionLayer::get_cell_size);
	ClassDB::bind_method(D_METHOD("set_load_radius", "load_radius"), &WorldPartitionLayer::set_load_radius);
	ClassDB::bind_method(D_METHOD("get_load_radius"), &WorldPartitionLayer::get_load_radius);
	ClassDB::bind_method(D_METHOD("set_evict_radius", "evict_radius"), &WorldPartitionLayer::set_evict_radius);
	ClassDB::bind_method(D_METHOD("get_evict_radius"), &WorldPartitionLayer::get_evict_radius);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "layer_id"), "set_layer_id", "get_layer_id");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cell_size", PROPERTY_HINT_NONE, "suffix:m"), "set_cell_size", "get_cell_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "load_radius", PROPERTY_HINT_NONE, "suffix:m"), "set_load_radius", "get_load_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "evict_radius", PROPERTY_HINT_NONE, "suffix:m"), "set_evict_radius", "get_evict_radius");
}

void WorldPartitionCell::BundlePath::set(const String &p_path) {
	path = p_path;
	valid = path.is_empty();
	if (valid || !path.begins_with("res://")) {
		return;
	}

	// Check components before simplify_path(), which would erase traversal evidence.
	const String relative = path.substr(6).replace_char('\\', '/');
	if (relative.begins_with("/") || relative.ends_with("/")) {
		return;
	}
	for (const String &component : relative.split("/", false)) {
		if (component == ".") {
			continue;
		}
		if (component.ends_with(".") || !component.is_valid_filename() || component != component.strip_escapes()) {
			return;
		}
	}

	const String extension = relative.get_extension().to_lower();
	for (const char *allowed : bundle_extensions) {
		if (extension == allowed) {
			path = path.simplify_path();
			valid = true;
			return;
		}
	}
}

bool WorldPartitionCell::has_valid_bundles() const {
	return hlod_impostor_path.valid && mid_mesh_path.valid && high_scene_path.valid && collision_bundle_path.valid &&
			(!hlod_impostor_path.path.is_empty() || !mid_mesh_path.path.is_empty() || !high_scene_path.path.is_empty() || !collision_bundle_path.path.is_empty());
}

void WorldPartitionCell::_bind_methods() {
	String file_filter;
	for (const char *extension : bundle_extensions) {
		if (!file_filter.is_empty()) {
			file_filter += ",";
		}
		file_filter += "*.";
		file_filter += extension;
	}

	ClassDB::bind_method(D_METHOD("set_cell_id", "cell_id"), &WorldPartitionCell::set_cell_id);
	ClassDB::bind_method(D_METHOD("get_cell_id"), &WorldPartitionCell::get_cell_id);
	ClassDB::bind_method(D_METHOD("set_layer", "layer"), &WorldPartitionCell::set_layer);
	ClassDB::bind_method(D_METHOD("get_layer"), &WorldPartitionCell::get_layer);
	ClassDB::bind_method(D_METHOD("set_grid_coords", "grid_coords"), &WorldPartitionCell::set_grid_coords);
	ClassDB::bind_method(D_METHOD("get_grid_coords"), &WorldPartitionCell::get_grid_coords);
	ClassDB::bind_method(D_METHOD("set_hlod_impostor_path", "hlod_impostor_path"), &WorldPartitionCell::set_hlod_impostor_path);
	ClassDB::bind_method(D_METHOD("get_hlod_impostor_path"), &WorldPartitionCell::get_hlod_impostor_path);
	ClassDB::bind_method(D_METHOD("set_mid_mesh_path", "mid_mesh_path"), &WorldPartitionCell::set_mid_mesh_path);
	ClassDB::bind_method(D_METHOD("get_mid_mesh_path"), &WorldPartitionCell::get_mid_mesh_path);
	ClassDB::bind_method(D_METHOD("set_high_scene_path", "high_scene_path"), &WorldPartitionCell::set_high_scene_path);
	ClassDB::bind_method(D_METHOD("get_high_scene_path"), &WorldPartitionCell::get_high_scene_path);
	ClassDB::bind_method(D_METHOD("set_collision_bundle_path", "collision_bundle_path"), &WorldPartitionCell::set_collision_bundle_path);
	ClassDB::bind_method(D_METHOD("get_collision_bundle_path"), &WorldPartitionCell::get_collision_bundle_path);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "cell_id"), "set_cell_id", "get_cell_id");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "layer"), "set_layer", "get_layer");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2I, "grid_coords"), "set_grid_coords", "get_grid_coords");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "hlod_impostor_path", PROPERTY_HINT_FILE, file_filter), "set_hlod_impostor_path", "get_hlod_impostor_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "mid_mesh_path", PROPERTY_HINT_FILE, file_filter), "set_mid_mesh_path", "get_mid_mesh_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "high_scene_path", PROPERTY_HINT_FILE, file_filter), "set_high_scene_path", "get_high_scene_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "collision_bundle_path", PROPERTY_HINT_FILE, file_filter), "set_collision_bundle_path", "get_collision_bundle_path");
}

WorldPartitionManifest::WorldPartitionManifest() {
	struct DefaultLayer {
		const char *id;
		double size;
	};
	constexpr DefaultLayer defaults[] = { { "macro", 512.0 }, { "meso", 128.0 }, { "micro", 32.0 } };
	for (const DefaultLayer &preset : defaults) {
		Ref<WorldPartitionLayer> layer;
		layer.instantiate();
		layer->set_layer_id(preset.id);
		layer->set_cell_size(preset.size);
		layer->set_load_radius(2.0 * preset.size);
		layer->set_evict_radius(3.0 * preset.size);
		layers.push_back(layer);
	}
}

Error WorldPartitionManifest::validate() const {
	if (layers.is_empty()) {
		return ERR_INVALID_DATA;
	}
	HashMap<StringName, HashSet<Vector2i>> coordinates_by_layer;
	for (const Ref<WorldPartitionLayer> layer : layers) {
		if (layer.is_null() || !is_valid_id(layer->get_layer_id()) || coordinates_by_layer.has(layer->get_layer_id())) {
			return ERR_INVALID_DATA;
		}
		const double size = layer->get_cell_size();
		const double load = layer->get_load_radius();
		const double evict = layer->get_evict_radius();
		if (!Math::is_finite(size) || !Math::is_finite(load) || !Math::is_finite(evict) || size <= 0.0 || load <= 0.0 || evict <= load) {
			return ERR_INVALID_DATA;
		}
		coordinates_by_layer.insert(layer->get_layer_id(), HashSet<Vector2i>());
	}

	HashSet<StringName> cell_ids;
	for (const Ref<WorldPartitionCell> cell : cells) {
		if (cell.is_null() || !is_valid_id(cell->get_cell_id()) || cell_ids.has(cell->get_cell_id()) || !cell->has_valid_bundles()) {
			return ERR_INVALID_DATA;
		}
		HashSet<Vector2i> *layer_coordinates = coordinates_by_layer.getptr(cell->get_layer());
		if (!layer_coordinates || layer_coordinates->has(cell->get_grid_coords())) {
			return ERR_INVALID_DATA;
		}
		layer_coordinates->insert(cell->get_grid_coords());
		cell_ids.insert(cell->get_cell_id());
	}
	return OK;
}

void WorldPartitionManifest::_bind_methods() {
	ClassDB::bind_method(D_METHOD("validate"), &WorldPartitionManifest::validate);
	ClassDB::bind_method(D_METHOD("set_layers", "layers"), &WorldPartitionManifest::set_layers);
	ClassDB::bind_method(D_METHOD("get_layers"), &WorldPartitionManifest::get_layers);
	ClassDB::bind_method(D_METHOD("set_cells", "cells"), &WorldPartitionManifest::set_cells);
	ClassDB::bind_method(D_METHOD("get_cells"), &WorldPartitionManifest::get_cells);

	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "layers", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("WorldPartitionLayer")), "set_layers", "get_layers");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "cells", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("WorldPartitionCell")), "set_cells", "get_cells");
}
