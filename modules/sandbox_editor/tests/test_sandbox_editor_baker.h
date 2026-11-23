/**************************************************************************/
/*  test_sandbox_editor_baker.h                                           */
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

#include "../sandbox_hlod_baker.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/packed_scene.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

#include "modules/sandbox_streaming/world_partition_manifest.h"

namespace TestSandboxEditorBaker {

struct BakeFixture {
	String previous_project = TestProjectSettingsInternalsAccessor::resource_path();
	Ref<DirAccess> project;
	Node3D *root = memnew(Node3D);

	BakeFixture() {
		Error err;
		project = DirAccess::create_temp("sandbox_baker", false, &err);
		if (project.is_valid()) {
			TestProjectSettingsInternalsAccessor::resource_path() = project->get_current_dir();
		}
	}

	~BakeFixture() {
		TestProjectSettingsInternalsAccessor::resource_path() = previous_project;
		memdelete(root);
	}
};

TEST_CASE("[SceneTree][SandboxEditor][HLODBaker] Tagged geometry round trips into cell-local scenes and one atlas surface") {
	BakeFixture fixture;
	REQUIRE(fixture.project.is_valid());
	Node3D *tagged = memnew(Node3D);
	tagged->add_to_group(SNAME("sandbox_static"));
	tagged->set_position(Vector3(-12, 0, 0));
	fixture.root->add_child(tagged);
	Ref<BoxMesh> box;
	box.instantiate();
	Ref<StandardMaterial3D> red;
	red.instantiate();
	red->set_albedo(Color(1, 0, 0));
	Ref<StandardMaterial3D> mapped;
	mapped.instantiate();
	Ref<Image> pixels = Image::create_empty(2, 1, false, Image::FORMAT_RGBA8);
	pixels->set_pixel(0, 0, Color(0, 0, 1));
	pixels->set_pixel(1, 0, Color(0, 1, 0));
	Ref<ImageTexture> map = ImageTexture::create_from_image(pixels);
	mapped->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, map);
	mapped->set_texture(BaseMaterial3D::TEXTURE_NORMAL, map);
	mapped->set_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING, true);
	mapped->set_roughness(0.5);
	mapped->set_metallic(0.75);
	for (int i = 0; i < 4; i++) {
		MeshInstance3D *instance = memnew(MeshInstance3D);
		instance->set_mesh(box);
		instance->set_material_override(i == 1 ? mapped : red);
		instance->set_position(Vector3(i == 2 ? 150 : 2 * i, 0, 0));
		(i == 3 ? fixture.root : tagged)->add_child(instance);
	}
	String path;
	String message;
	REQUIRE_MESSAGE(SandboxHLODBaker::bake_scene(fixture.root, "res://baked", "meso", 64, path, message) == OK, message.utf8().get_data());
	Ref<WorldPartitionManifest> manifest = ResourceLoader::load(path);
	REQUIRE(manifest.is_valid());
	CHECK(manifest->validate() == OK);
	REQUIRE(manifest->get_cells().size() == 2);
	const Ref<WorldPartitionCell> cell = manifest->get_cells()[0];
	CHECK(cell->get_grid_coords() == Vector2i(-1, 0));
	Ref<PackedScene> high = ResourceLoader::load(cell->get_high_scene_path());
	REQUIRE(high.is_valid());
	Node *baked = high->instantiate();
	REQUIRE(baked != nullptr);
	fixture.root->add_child(baked);
	REQUIRE(baked->get_child_count() == 2);
	MeshInstance3D *first = Object::cast_to<MeshInstance3D>(baked->get_child(0));
	REQUIRE(first);
	CHECK(first->get_position().is_equal_approx(Vector3(116, 0, 0)));
	Ref<ArrayMesh> proxy = ResourceLoader::load(cell->get_hlod_impostor_path());
	REQUIRE(proxy.is_valid());
	REQUIRE(proxy->get_surface_count() == 1);
	const AABB expected(Vector3(115.5, -0.5, -0.5), Vector3(3, 1, 1));
	CHECK(proxy->get_aabb().is_equal_approx(expected));
	const Array arrays = proxy->surface_get_arrays(0);
	CHECK(PackedInt32Array(arrays[Mesh::ARRAY_INDEX]).size() == 72);
	REQUIRE_FALSE(PackedFloat32Array(arrays[Mesh::ARRAY_CUSTOM0]).is_empty());
	Ref<ShaderMaterial> material = proxy->surface_get_material(0);
	REQUIRE(material.is_valid());
	Ref<Texture2D> albedo = material->get_shader_parameter("albedo_atlas");
	Ref<Texture2D> normal = material->get_shader_parameter("normal_atlas");
	Ref<Texture2D> orm = material->get_shader_parameter("orm_atlas");
	REQUIRE(albedo.is_valid());
	REQUIRE(normal.is_valid());
	REQUIRE(orm.is_valid());
	Ref<Image> baked_albedo = albedo->get_image();
	Ref<Image> baked_normals = normal->get_image();
	Ref<Image> baked_orm = orm->get_image();
	REQUIRE(baked_albedo.is_valid());
	REQUIRE(baked_normals.is_valid());
	REQUIRE(baked_orm.is_valid());
	CHECK(baked_albedo->get_size() == baked_normals->get_size());
	CHECK(baked_albedo->get_size() == baked_orm->get_size());
	const PackedFloat32Array rects = arrays[Mesh::ARRAY_CUSTOM0];
	Vector<Color> distinct;
	for (int i = 0; i + 3 < rects.size(); i += 4) {
		Color rect(rects[i], rects[i + 1], rects[i + 2], rects[i + 3]);
		if (!distinct.has(rect)) {
			distinct.push_back(rect);
		}
	}
	REQUIRE(distinct.size() == 2);
	const int w = baked_albedo->get_width();
	const int h = baked_albedo->get_height();
	int matched_materials = 0;
	for (const Color &rect : distinct) {
		const Vector2i pixel(Math::round(rect.r * w), Math::round(rect.g * h));
		const Color at = baked_albedo->get_pixelv(pixel);
		if (at.r > 0.99f) {
			CHECK(at.b == 0);
			CHECK(baked_orm->get_pixelv(pixel).b == 0);
			matched_materials |= 1;
		} else if (at.b > 0.99f) {
			CHECK(baked_albedo->get_pixelv(pixel + Vector2i(1, 0)).g > 0.99f);
			CHECK(baked_normals->get_pixelv(pixel).b > 0.99f);
			const Color surface = baked_orm->get_pixelv(pixel);
			CHECK(Math::abs(surface.g - 0.5f) < 0.01f);
			CHECK(Math::abs(surface.b - 0.75f) < 0.01f);
			matched_materials |= 2;
		}
	}
	CHECK(matched_materials == 3);
	CHECK(tagged->get_child_count() == 3);
}

TEST_CASE("[SceneTree][SandboxEditor][HLODBaker] Invalid paths and failed later cells leave no partial generation") {
	BakeFixture fixture;
	REQUIRE(fixture.project.is_valid());
	String path;
	String message;
	CHECK(SandboxHLODBaker::bake_scene(fixture.root, "res://../outside", "meso", 8, path, message) == ERR_FILE_BAD_PATH);
	CHECK(SandboxHLODBaker::bake_scene(fixture.root, "res://cells", "meso", 8, path, message) == ERR_DOES_NOT_EXIST);
	CHECK_FALSE(DirAccess::dir_exists_absolute("res://cells"));
	fixture.root->add_to_group("sandbox_static");
	Ref<BoxMesh> box;
	box.instantiate();
	for (int i = 0; i < 2; i++) {
		MeshInstance3D *instance = memnew(MeshInstance3D);
		instance->set_mesh(box);
		instance->set_position(Vector3(i * 512, 0, 0));
		fixture.root->add_child(instance);
	}
	REQUIRE(SandboxHLODBaker::bake_scene(fixture.root, "res://cells", "macro", 8, path, message) == OK);
	String previous = path;
	Ref<ShaderMaterial> unsupported;
	unsupported.instantiate();
	Object::cast_to<MeshInstance3D>(fixture.root->get_child(1))->set_material_override(unsupported);
	CHECK(SandboxHLODBaker::bake_scene(fixture.root, "res://cells", "macro", 8, path, message) == ERR_UNAVAILABLE);
	CHECK(path.is_empty());
	CHECK(FileAccess::exists(previous));
	CHECK_FALSE(DirAccess::dir_exists_absolute("res://cells/macro_2"));
}

} // namespace TestSandboxEditorBaker
