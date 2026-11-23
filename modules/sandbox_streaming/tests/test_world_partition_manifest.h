/**************************************************************************/
/*  test_world_partition_manifest.h                                       */
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

#include "../world_partition_manifest.h"

#include "tests/test_macros.h"

#include <limits>

namespace TestWorldPartitionManifest {

static Ref<WorldPartitionCell> make_cell(const StringName &p_id, const StringName &p_layer) {
	Ref<WorldPartitionCell> cell;
	cell.instantiate();
	cell->set_cell_id(p_id);
	cell->set_layer(p_layer);
	cell->set_grid_coords(Vector2i(-5, 3));
	cell->set_high_scene_path("res://world/city.tscn");
	return cell;
}

TEST_CASE("[SandboxStreaming][Gate5] All bundle slots validate project paths before normalization") {
	Ref<WorldPartitionManifest> manifest;
	manifest.instantiate();
	Ref<WorldPartitionCell> cell = make_cell("city", "meso");
	manifest->get_cells().push_back(cell);
	REQUIRE(manifest->validate() == OK);

	cell->set_high_scene_path("res://world//./blocks\\city.tscn");
	REQUIRE(manifest->validate() == OK);
	CHECK(cell->get_high_scene_path() == "res://world/blocks/city.tscn");

	const char *invalid_paths[] = {
		"../city.tscn",
		"user://cache/city.tscn",
		"/world/city.tscn",
		"C:\\world\\city.tscn",
		"res://world/../city.tscn",
		"res://world\\..\\city.tscn",
		"res:///world/city.tscn",
		"res://C:/city.tscn",
		"res://city.tscn::sub.tres",
		"res://world/tool.gd",
		"res://world/tool.GDC",
		"res://world/tool.cs",
		"res://world/tool.gdextension",
	};
	for (const char *invalid_path : invalid_paths) {
		CAPTURE(invalid_path);
		cell->set_high_scene_path(invalid_path);
		CHECK(manifest->validate() == ERR_INVALID_DATA);
	}
	cell->set_high_scene_path("res://world/city.tscn");
	cell->set_mid_mesh_path("res://../street.mesh");
	CHECK(manifest->validate() == ERR_INVALID_DATA);
	cell->set_mid_mesh_path("res://world/street.mesh");
	cell->set_hlod_impostor_path("res://../skyline.tres");
	CHECK(manifest->validate() == ERR_INVALID_DATA);
	cell->set_hlod_impostor_path("res://world/skyline.tres");
	cell->set_collision_bundle_path("res://../shapes.res");
	CHECK(manifest->validate() == ERR_INVALID_DATA);
	cell->set_collision_bundle_path("res://world/shapes.res");
	CHECK(manifest->validate() == OK);

	cell->set_hlod_impostor_path("");
	cell->set_mid_mesh_path("");
	cell->set_high_scene_path("");
	CHECK(manifest->validate() == OK); // Collision-only cell.
	cell->set_collision_bundle_path("");
	CHECK(manifest->validate() == ERR_INVALID_DATA);
}

TEST_CASE("[SandboxStreaming] Manifest enforces grid identity and layer hysteresis") {
	Ref<WorldPartitionManifest> manifest;
	manifest.instantiate();
	TypedArray<WorldPartitionLayer> layers = manifest->get_layers();
	REQUIRE(layers.size() == 3);
	Ref<WorldPartitionLayer> macro = layers[0];
	Ref<WorldPartitionLayer> meso = layers[1];
	Ref<WorldPartitionLayer> micro = layers[2];
	CHECK(macro->get_cell_size() == 512.0);
	CHECK(meso->get_cell_size() == 128.0);
	CHECK(micro->get_cell_size() == 32.0);
	REQUIRE(manifest->validate() == OK);

	SUBCASE("Invalid layer dimensions") {
		macro->set_cell_size(0.0);
		CHECK(manifest->validate() == ERR_INVALID_DATA);
		macro->set_cell_size(512.0);
		macro->set_load_radius(std::numeric_limits<double>::quiet_NaN());
		CHECK(manifest->validate() == ERR_INVALID_DATA);
		macro->set_load_radius(50.0);
		macro->set_evict_radius(50.0);
		CHECK(manifest->validate() == ERR_INVALID_DATA);
		macro->set_evict_radius(std::numeric_limits<double>::infinity());
		CHECK(manifest->validate() == ERR_INVALID_DATA);
	}
	SUBCASE("Layer IDs are unique and required") {
		meso->set_layer_id(macro->get_layer_id());
		CHECK(manifest->validate() == ERR_INVALID_DATA);
		meso->set_layer_id(StringName());
		CHECK(manifest->validate() == ERR_INVALID_DATA);
		layers.clear();
		CHECK(manifest->validate() == ERR_INVALID_DATA);
	}
	SUBCASE("Layer coordinates are local and cell IDs are global") {
		Ref<WorldPartitionCell> first = make_cell("street", "micro");
		Ref<WorldPartitionCell> second = make_cell("block", "meso");
		manifest->get_cells().push_back(first);
		manifest->get_cells().push_back(second);
		CHECK(manifest->validate() == OK);
		second->set_layer("micro");
		CHECK(manifest->validate() == ERR_INVALID_DATA);
		second->set_layer("unregistered");
		CHECK(manifest->validate() == ERR_INVALID_DATA);
		second->set_layer("meso");
		second->set_cell_id(first->get_cell_id());
		CHECK(manifest->validate() == ERR_INVALID_DATA);
		second->set_cell_id(StringName());
		CHECK(manifest->validate() == ERR_INVALID_DATA);
	}
}

} // namespace TestWorldPartitionManifest
