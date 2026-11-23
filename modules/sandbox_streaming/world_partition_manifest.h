/**************************************************************************/
/*  world_partition_manifest.h                                            */
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
#include "core/math/vector2i.h"
#include "core/variant/typed_array.h"

class WorldPartitionLayer : public Resource {
	GDCLASS(WorldPartitionLayer, Resource);

	StringName layer_id;
	double cell_size = 128.0;
	double load_radius = 256.0;
	double evict_radius = 384.0;

protected:
	static void _bind_methods();

public:
	void set_layer_id(const StringName &p_id) { layer_id = p_id; }
	StringName get_layer_id() const { return layer_id; }
	void set_cell_size(double p_size) { cell_size = p_size; }
	double get_cell_size() const { return cell_size; }
	void set_load_radius(double p_radius) { load_radius = p_radius; }
	double get_load_radius() const { return load_radius; }
	void set_evict_radius(double p_radius) { evict_radius = p_radius; }
	double get_evict_radius() const { return evict_radius; }
};

class WorldPartitionCell : public Resource {
	GDCLASS(WorldPartitionCell, Resource);

	// Keep invalid input editable so registration can reject the entire manifest.
	struct BundlePath {
		String path;
		bool valid = true;

		void set(const String &p_path);
	};

	StringName cell_id;
	StringName layer;
	Vector2i grid_coords;
	BundlePath hlod_impostor_path;
	BundlePath mid_mesh_path;
	BundlePath high_scene_path;
	BundlePath collision_bundle_path;

protected:
	static void _bind_methods();

public:
	void set_cell_id(const StringName &p_id) { cell_id = p_id; }
	StringName get_cell_id() const { return cell_id; }
	void set_layer(const StringName &p_layer) { layer = p_layer; }
	StringName get_layer() const { return layer; }
	void set_grid_coords(const Vector2i &p_coords) { grid_coords = p_coords; }
	Vector2i get_grid_coords() const { return grid_coords; }
	void set_hlod_impostor_path(const String &p_path) { hlod_impostor_path.set(p_path); }
	String get_hlod_impostor_path() const { return hlod_impostor_path.path; }
	void set_mid_mesh_path(const String &p_path) { mid_mesh_path.set(p_path); }
	String get_mid_mesh_path() const { return mid_mesh_path.path; }
	void set_high_scene_path(const String &p_path) { high_scene_path.set(p_path); }
	String get_high_scene_path() const { return high_scene_path.path; }
	void set_collision_bundle_path(const String &p_path) { collision_bundle_path.set(p_path); }
	String get_collision_bundle_path() const { return collision_bundle_path.path; }
	bool has_valid_bundles() const;
};

class WorldPartitionManifest : public Resource {
	GDCLASS(WorldPartitionManifest, Resource);

	TypedArray<WorldPartitionLayer> layers;
	TypedArray<WorldPartitionCell> cells;

protected:
	static void _bind_methods();

public:
	void set_layers(const TypedArray<WorldPartitionLayer> &p_layers) { layers = p_layers; }
	TypedArray<WorldPartitionLayer> get_layers() const { return layers; }
	void set_cells(const TypedArray<WorldPartitionCell> &p_cells) { cells = p_cells; }
	TypedArray<WorldPartitionCell> get_cells() const { return cells; }
	Error validate() const;

	WorldPartitionManifest();
};
