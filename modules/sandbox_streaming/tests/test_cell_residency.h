/**************************************************************************/
/*  test_cell_residency.h                                                 */
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

#include "../cell_residency.h"

#include "core/io/resource_loader.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/material.h"
#include "scene/resources/shader.h"
#include "tests/test_macros.h"

namespace TestCellResidency {

TEST_CASE("[SceneTree][SandboxStreaming][Gate6] Staged batches respect an expired deadline and preserve the previous LOD on failure") {
	const Vector3 placement(19, 0, 7);
	Node3D *parent = memnew(Node3D);
	parent->set_position(Vector3(3145728, 0, 0));
	SceneTree::get_singleton()->get_root()->add_child(parent);

	Ref<PackedScene> scene;
	scene.instantiate();
	Node3D *batch = memnew(Node3D);
	CHECK(scene->pack(batch) == OK);
	memdelete(batch);
	TypedArray<PackedScene> batches;
	batches.push_back(scene);
	batches.push_back(scene);
	Ref<CellSceneBundle> bundle;
	bundle.instantiate();
	bundle->set_batches(batches);
	CellResidency::Paths paths;
	paths.proxy = "res://sandbox_test_batches.tres";
	bundle->set_path(paths.proxy);

	CellResidency residency;
	auto tick = [&](CellResidency::Lod p_lod, uint64_t p_deadline, CellResidency::SceneStep *p_step = nullptr) {
		return residency.tick(p_lod, paths, parent, placement, 1.0 / 60.0, p_deadline, p_step);
	};
	CHECK_FALSE(tick(CellResidency::LOD_PROXY, UINT64_MAX));
	CHECK(residency.get_state() == CellResidency::LOADING_ASYNC);
	CHECK_FALSE(tick(CellResidency::LOD_PROXY, 0));
	CHECK(residency.get_state() == CellResidency::STAGED);
	CHECK(residency.get_root() == nullptr);
	CellResidency::SceneStep late{ 0 };
	CHECK_FALSE(tick(CellResidency::LOD_PROXY, UINT64_MAX, &late));
	CHECK_FALSE(late.taken);
	// Two independent subtrees, one instantiation or attachment per frame.
	for (int frame = 0; frame < 4; frame++) {
		CellResidency::SceneStep step{ UINT64_MAX };
		CHECK(tick(CellResidency::LOD_PROXY, UINT64_MAX, &step) == (frame == 3));
		CHECK(step.taken);
	}
	Node3D *anchor = residency.get_root();
	REQUIRE(anchor != nullptr);
	CHECK(anchor->get_global_position() == placement);
	REQUIRE(anchor->get_child_count() == 1);
	CHECK(anchor->get_child(0)->get_child_count() == 2);

	// A partially attached replacement must roll back only its new nodes.
	Ref<CellSceneBundle> invalid;
	invalid.instantiate();
	TypedArray<PackedScene> incomplete;
	incomplete.push_back(scene);
	incomplete.push_back(Ref<PackedScene>());
	invalid->set_batches(incomplete);
	paths.high = "res://sandbox_test_invalid_batch.tres";
	invalid->set_path(paths.high);
	CHECK_FALSE(tick(CellResidency::LOD_HIGH, UINT64_MAX));
	CHECK_FALSE(tick(CellResidency::LOD_HIGH, UINT64_MAX));
	CHECK(residency.get_last_error() == ERR_INVALID_DATA);
	CHECK(residency.get_active_lod() == CellResidency::LOD_PROXY);
	CHECK(residency.get_state() == CellResidency::ACTIVE_ATTACHED);
	CHECK(anchor->get_child_count() == 1);
	CHECK_FALSE(residency.has_pending_request());

	CHECK_FALSE(tick(CellResidency::LOD_NONE, 0));
	CHECK(residency.get_root() == anchor);
	CHECK_FALSE(tick(CellResidency::LOD_NONE, UINT64_MAX));
	CHECK(residency.get_root() == nullptr);
	CHECK_FALSE(tick(CellResidency::LOD_NONE, UINT64_MAX));
	CHECK(residency.get_state() == CellResidency::UNLOADED);
}

TEST_CASE("[SceneTree][SandboxStreaming][Gate6] Cancelled cells balance shared resource loader requests") {
	Node3D *parent = memnew(Node3D);
	SceneTree::get_singleton()->get_root()->add_child(parent);
	Ref<PackedScene> scene;
	scene.instantiate();
	CellResidency::Paths paths;
	paths.high = "res://sandbox_test_shared_request.tscn";
	scene->set_path(paths.high);
	CellResidency a;
	CellResidency b;
	a.tick(CellResidency::LOD_HIGH, paths, parent, Vector3(), 0, UINT64_MAX);
	b.tick(CellResidency::LOD_HIGH, paths, parent, Vector3(), 0, UINT64_MAX);
	REQUIRE(a.has_pending_request());
	REQUIRE(b.has_pending_request());
	a.tick(CellResidency::LOD_NONE, paths, parent, Vector3(), 0, UINT64_MAX);
	a.tick(CellResidency::LOD_NONE, paths, parent, Vector3(), 0, UINT64_MAX);
	CHECK_FALSE(a.has_pending_request());
	CHECK(ResourceLoader::load_threaded_get_status(paths.high) == ResourceLoader::THREAD_LOAD_LOADED);
	b.clear();
	CHECK_FALSE(b.has_pending_request());
	CHECK(ResourceLoader::load_threaded_get_status(paths.high) == ResourceLoader::THREAD_LOAD_INVALID_RESOURCE);
}

TEST_CASE("[SceneTree][SandboxStreaming][Gate6] HLOD swaps supply complementary screen-door uniforms to live materials") {
	Node3D *parent = memnew(Node3D);
	SceneTree::get_singleton()->get_root()->add_child(parent);

	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(R"(
shader_type spatial;
instance uniform float hlod_fade = 1.0;
instance uniform bool hlod_fade_outgoing = false;
void fragment() {
	vec2 pixel = mod(floor(FRAGCOORD.xy), vec2(4.0));
	float threshold = (pixel.x + pixel.y * 4.0 + 0.5) / 16.0;
	if (hlod_fade_outgoing ? threshold < 1.0 - hlod_fade : threshold >= hlod_fade) {
		discard;
	}
}
)");
	Ref<ShaderMaterial> material;
	material.instantiate();
	material->set_shader(shader);
	CellResidency::Paths paths;
	paths.proxy = "res://sandbox_test_dither_proxy.tres";
	paths.high = "res://sandbox_test_dither_high.tres";
	Ref<BoxMesh> proxy;
	proxy.instantiate();
	proxy->set_material(material);
	proxy->set_path(paths.proxy);
	Ref<BoxMesh> high;
	high.instantiate();
	high->set_material(material);
	high->set_path(paths.high);

	CellResidency residency;
	auto tick = [&](CellResidency::Lod p_lod, double p_delta = 0.0) {
		return residency.tick(p_lod, paths, parent, Vector3(), p_delta, UINT64_MAX);
	};
	CHECK_FALSE(tick(CellResidency::LOD_PROXY));
	REQUIRE(tick(CellResidency::LOD_PROXY));
	Node3D *anchor = residency.get_root();
	CHECK_FALSE(tick(CellResidency::LOD_HIGH));
	REQUIRE(tick(CellResidency::LOD_HIGH));
	REQUIRE(anchor->get_child_count() == 2);
	auto *outgoing = Object::cast_to<MeshInstance3D>(anchor->get_child(0)->get_child(0)->get_child(0));
	auto *incoming = Object::cast_to<MeshInstance3D>(anchor->get_child(1)->get_child(0)->get_child(0));
	REQUIRE(outgoing);
	REQUIRE(incoming);
	CHECK(float(outgoing->get_instance_shader_parameter("hlod_fade")) == 1.0f);
	CHECK(float(incoming->get_instance_shader_parameter("hlod_fade")) == 0.0f);
	CHECK_FALSE(tick(CellResidency::LOD_HIGH, 0.125));
	CHECK(float(outgoing->get_instance_shader_parameter("hlod_fade")) == 0.5f);
	CHECK(float(incoming->get_instance_shader_parameter("hlod_fade")) == 0.5f);
	CHECK(bool(outgoing->get_instance_shader_parameter("hlod_fade_outgoing")));
	CHECK_FALSE(bool(incoming->get_instance_shader_parameter("hlod_fade_outgoing")));
	CHECK_FALSE(tick(CellResidency::LOD_HIGH, 0.125));
	CHECK(anchor->get_child_count() == 1);
	CHECK(float(incoming->get_instance_shader_parameter("hlod_fade")) == 1.0f);
}

} // namespace TestCellResidency
