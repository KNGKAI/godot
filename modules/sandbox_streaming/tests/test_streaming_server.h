/**************************************************************************/
/*  test_streaming_server.h                                               */
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

#include "../streaming_invoker.h"
#include "../streaming_server.h"

#include "core/config/engine.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "scene/3d/node_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

#include "modules/sandbox_core/command_bus.h"

#include <limits>

namespace TestStreamingServer {

struct ResetStreaming {
	StreamingServer *server;

	~ResetStreaming() {
		server->set_enabled(false);
		server->set_manifest(Ref<WorldPartitionManifest>());
		server->set_budget_ms(2.0);
		server->set_hlod_force("auto");
	}
};

TEST_CASE("[SandboxStreaming] Registered server is opt-in and validates runtime settings") {
	StreamingServer *server = StreamingServer::get_singleton();
	REQUIRE(server != nullptr);
	CHECK(Engine::get_singleton()->get_singleton_object("StreamingServer") == server);
	CHECK_FALSE(server->is_enabled());
	ResetStreaming reset{ server };
	CommandBus *bus = CommandBus::get_singleton();
	REQUIRE(bus != nullptr);
	for (const char *command : { "stream.stats", "stream.pin", "stream.unpin", "stream.budget", "stream.hlod_force" }) {
		CHECK(bus->has_command(command));
	}
	REQUIRE(server->set_budget_ms(0.5) == OK);
	for (double invalid : { -1.0, 0.0, 2.1, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() }) {
		CHECK(server->set_budget_ms(invalid) == ERR_INVALID_PARAMETER);
		CHECK(double(server->get_stats()["budget_ms"]) == 0.5);
	}
	CHECK(server->set_hlod_force("1") == OK);
	CHECK(server->set_hlod_force("0") == OK);
	CHECK(server->set_hlod_force("auto") == OK);
	CHECK(server->set_hlod_force("unknown") == ERR_INVALID_PARAMETER);
}

TEST_CASE("[SandboxStreaming][Gate5] Registration atomically replaces a snapshot of validated cells") {
	StreamingServer *server = StreamingServer::get_singleton();
	REQUIRE(server != nullptr);
	ResetStreaming reset{ server };
	Ref<WorldPartitionManifest> manifest;
	manifest.instantiate();
	Ref<WorldPartitionCell> cell;
	cell.instantiate();
	cell->set_cell_id("block");
	cell->set_layer("meso");
	const Vector2i first(-4, 7);
	const Vector2i moved(8, -1);
	cell->set_grid_coords(first);
	cell->set_high_scene_path("res://world/block.tscn");
	manifest->get_cells().push_back(cell);
	REQUIRE(server->set_manifest(manifest) == OK);
	cell->set_grid_coords(moved);
	CHECK(server->set_pinned("meso", first, true) == OK);
	CHECK(server->set_pinned("meso", moved, true) == ERR_DOES_NOT_EXIST);
	cell->set_high_scene_path("res://world/../block.tscn");
	CHECK(server->set_manifest(manifest) == ERR_INVALID_DATA);
	CHECK(server->set_pinned("meso", first, false) == OK);
	CHECK(server->set_pinned("meso", moved, true) == ERR_DOES_NOT_EXIST);
	cell->set_high_scene_path("res://world/block.tscn");
	REQUIRE(server->set_manifest(manifest) == OK);
	CHECK(server->set_pinned("meso", first, true) == ERR_DOES_NOT_EXIST);
	CHECK(server->set_pinned("meso", moved, true) == OK);
}

struct RestoreResourcePath {
	String original = TestProjectSettingsInternalsAccessor::resource_path();
	~RestoreResourcePath() { TestProjectSettingsInternalsAccessor::resource_path() = original; }
};

static bool write_fixture(const String &p_path, const String &p_text) {
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	return file.is_valid() && file->store_string(p_text);
}

TEST_CASE("[SceneTree][SandboxStreaming][Gate6] Pinning loads scene and collision assets and eviction releases them") {
	StreamingServer *server = StreamingServer::get_singleton();
	SceneTree *tree = SceneTree::get_singleton();
	REQUIRE(server != nullptr);
	REQUIRE(tree != nullptr);
	Error error;
	Ref<DirAccess> directory = DirAccess::create_temp("sandboxstreaming", false, &error);
	REQUIRE(error == OK);
	REQUIRE(directory.is_valid());
	RestoreResourcePath restore_path;
	TestProjectSettingsInternalsAccessor::resource_path() = directory->get_current_dir();
	// Drain every pending load before the temporary project is unmounted, even after a failed assertion.
	ResetStreaming reset{ server };

	const String mesh_path = "res://streaming_test_mesh.tres";
	const String shape_path = "res://streaming_test_shape.tres";
	const String scene_path = "res://streaming_test_scene.tscn";
	const String proxy_path = "res://streaming_test_proxy.tscn";
	REQUIRE(write_fixture(mesh_path, "[gd_resource type=\"BoxMesh\" format=3]\n\n[resource]\n"));
	REQUIRE(write_fixture(shape_path, "[gd_resource type=\"BoxShape3D\" format=3]\n\n[resource]\n"));
	REQUIRE(write_fixture(scene_path,
			"[gd_scene load_steps=2 format=3]\n"
			"[ext_resource type=\"Mesh\" path=\"res://streaming_test_mesh.tres\" id=\"1\"]\n"
			"[node name=\"StreamedBuilding\" type=\"MeshInstance3D\"]\n"
			"mesh = ExtResource(\"1\")\n"));
	REQUIRE(write_fixture(proxy_path,
			"[gd_scene load_steps=2 format=3]\n"
			"[ext_resource type=\"Mesh\" path=\"res://streaming_test_mesh.tres\" id=\"1\"]\n"
			"[node name=\"StreamedProxy\" type=\"MeshInstance3D\"]\n"
			"mesh = ExtResource(\"1\")\n"));
	Node3D *world = memnew(Node3D);
	tree->get_root()->add_child(world);
	tree->set_current_scene(world);

	Ref<WorldPartitionManifest> manifest;
	manifest.instantiate();
	Ref<WorldPartitionCell> cell;
	cell.instantiate();
	cell->set_cell_id("building");
	cell->set_layer("meso");
	cell->set_collision_bundle_path(shape_path);
	cell->set_high_scene_path(scene_path);
	cell->set_hlod_impostor_path(proxy_path);
	manifest->get_cells().push_back(cell);
	REQUIRE(server->set_manifest(manifest) == OK);
	REQUIRE(server->set_pinned("meso", Vector2i(), true) == OK);
	server->set_enabled(true);
	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + 5000;
	do {
		server->update(1.0 / 60.0);
		const Dictionary stats = server->get_stats();
		if (int(stats["resident_cells"]) > 0 || int(stats["failed_cells"]) > 0) {
			break;
		}
		OS::get_singleton()->delay_usec(1000);
	} while (OS::get_singleton()->get_ticks_msec() < deadline);
	REQUIRE(int(server->get_stats()["resident_cells"]) == 1);
	CHECK(world->find_child("StreamedBuilding", true, false) != nullptr);
	CHECK(ResourceCache::has(mesh_path));
	CHECK(ResourceCache::has(shape_path));

	REQUIRE(server->set_hlod_force("1") == OK);
	const uint64_t proxy_deadline = OS::get_singleton()->get_ticks_msec() + 5000;
	Node3D *proxy = nullptr;
	do {
		server->update(1.0 / 60.0);
		proxy = Object::cast_to<Node3D>(world->find_child("StreamedProxy", true, false));
		if (proxy && proxy->is_visible_in_tree()) {
			break;
		}
		// Keep drawing the high-detail building while the proxy is in flight.
		CHECK(world->find_child("StreamedBuilding", true, false) != nullptr);
		OS::get_singleton()->delay_usec(1000);
	} while (OS::get_singleton()->get_ticks_msec() < proxy_deadline);
	REQUIRE(proxy != nullptr);
	REQUIRE(proxy->is_visible_in_tree());
	server->update(0.125);
	Node3D *building = Object::cast_to<Node3D>(world->find_child("StreamedBuilding", true, false));
	REQUIRE(building != nullptr);
	CHECK(building->is_visible_in_tree());
	CHECK(proxy->is_visible_in_tree());
	for (int i = 0; i < 15; i++) {
		server->update(1.0 / 60.0);
	}
	tree->process(1.0 / 60.0);
	CHECK(world->find_child("StreamedBuilding", true, false) == nullptr);
	CHECK_FALSE(ResourceCache::has(shape_path));
	CHECK(proxy->is_visible_in_tree());

	REQUIRE(server->set_pinned("meso", Vector2i(), false) == OK);
	server->update(1.0 / 60.0);
	tree->process(1.0 / 60.0); // Flush deferred node deletion.
	CHECK(int(server->get_stats()["resident_cells"]) == 0);
	CHECK(world->find_child("StreamedProxy", true, false) == nullptr);
	CHECK_FALSE(ResourceCache::has(mesh_path));
	CHECK_FALSE(ResourceCache::has(shape_path));
}

TEST_CASE("[SceneTree][SandboxStreaming] A moving invoker preloads along its path and eviction uses the wider radius") {
	StreamingServer *server = StreamingServer::get_singleton();
	ResetStreaming reset{ server };
	SceneTree *tree = SceneTree::get_singleton();
	Node3D *world = memnew(Node3D);
	tree->get_root()->add_child(world);
	tree->set_current_scene(world);

	Ref<PackedScene> scene;
	scene.instantiate();
	Node3D *landmark = memnew(Node3D);
	REQUIRE(scene->pack(landmark) == OK);
	memdelete(landmark);
	scene->set_path("res://test_streamed_landmark.tscn");
	Ref<WorldPartitionManifest> manifest;
	manifest.instantiate();
	Ref<WorldPartitionCell> cell;
	cell.instantiate();
	cell->set_cell_id("landmark");
	cell->set_layer("micro");
	cell->set_grid_coords(Vector2i(5, 0)); // x = [160, 192], load at 64 m, evict beyond 96 m.
	cell->set_high_scene_path(scene->get_path());
	manifest->get_cells().push_back(cell);
	REQUIRE(server->set_manifest(manifest) == OK);

	StreamingInvoker *invoker = memnew(StreamingInvoker);
	world->add_child(invoker);
	server->set_enabled(true);
	server->update(0.25);
	CHECK(int(server->get_stats()["resident_cells"]) == 0);
	invoker->set_position(Vector3(40, 0, 0)); // 120 m away; only lookahead can request it.
	server->update(0.25);
	for (int i = 0; i < 6; i++) {
		server->update(0.001);
	}
	REQUIRE(int(server->get_stats()["resident_cells"]) == 1);

	memdelete(invoker);
	invoker = memnew(StreamingInvoker);
	invoker->set_position(Vector3(85, 0, 0)); // A stationary invoker is inside only the eviction radius.
	world->add_child(invoker);
	server->update(0.25);
	CHECK(int(server->get_stats()["resident_cells"]) == 1);
	invoker->set_position(Vector3());
	for (int i = 0; i < 4; i++) {
		server->update(0.25);
	}
	CHECK(int(server->get_stats()["resident_cells"]) == 0);
}

} // namespace TestStreamingServer
