/**************************************************************************/
/*  sandbox_hlod_baker.cpp                                                */
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

#include "sandbox_hlod_baker.h"

#include "../sandbox_streaming/world_partition_manifest.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_saver.h"
#include "core/math/geometry_2d.h"
#include "core/object/callable_mp.h"
#include "core/string/translation.h"
#include "editor/editor_interface.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/gui/editor_file_dialog.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/spin_box.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/packed_scene.h"
#include "scene/resources/surface_tool.h"

namespace {

constexpr int ATLAS_LIMIT = 4096;
constexpr int GUTTER = 1;

struct SourceMesh {
	MeshInstance3D *instance = nullptr;
	Transform3D local_to_cell;
};

using CellMap = HashMap<Vector2i, Vector<SourceMesh>>;

Error collect_meshes(Node *p_node, const Transform3D &p_parent_transform, bool p_tagged, real_t p_size, CellMap &r_cells, String &r_message) {
	const bool tagged = p_tagged || p_node->is_in_group(SNAME("sandbox_static"));
	Transform3D transform;
	if (Node3D *spatial = Object::cast_to<Node3D>(p_node)) {
		if (!spatial->is_visible()) {
			return OK;
		}
		transform = spatial->is_set_as_top_level() ? spatial->get_transform() : p_parent_transform * spatial->get_transform();
	}
	MeshInstance3D *instance = Object::cast_to<MeshInstance3D>(p_node);
	if (tagged && instance && instance->get_mesh().is_valid()) {
		const double x = Math::floor(double(transform.origin.x) / p_size);
		const double z = Math::floor(double(transform.origin.z) / p_size);
		if (!transform.is_finite() || transform.basis.determinant() == 0 || x < INT32_MIN || x > INT32_MAX || z < INT32_MIN || z > INT32_MAX) {
			r_message = vformat(TTR("Invalid static mesh transform on %s."), instance->get_name());
			return ERR_INVALID_DATA;
		}
		const Vector2i coords{ int32_t(x), int32_t(z) };
		Transform3D local = transform;
		local.origin -= Vector3(x * p_size, 0, z * p_size);
		r_cells[coords].push_back({ instance, local });
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		Error err = collect_meshes(p_node->get_child(i), transform, tagged, p_size, r_cells, r_message);
		if (err != OK) {
			return err;
		}
	}
	return OK;
}

struct Tile {
	Ref<BaseMaterial3D> material;
	Size2i size = Size2i(1, 1);
	Color rect;
	Ref<Image> images[BaseMaterial3D::TEXTURE_MAX];

	Color pixel(BaseMaterial3D::TextureParam p_param, int p_x, int p_y, Color p_default = Color(1, 1, 1)) const {
		return images[p_param].is_valid() ? images[p_param]->get_pixel(p_x, p_y) : p_default;
	}
};

Error read_tile(const Ref<BaseMaterial3D> &p_material, int p_max_size, Tile &r_tile) {
	r_tile.material = p_material;
	if (p_material.is_null()) {
		return OK;
	}
	if (p_material->get_flag(BaseMaterial3D::FLAG_UV1_USE_TRIPLANAR) || (p_material->get_feature(BaseMaterial3D::FEATURE_AMBIENT_OCCLUSION) && p_material->get_flag(BaseMaterial3D::FLAG_AO_ON_UV2))) {
		return ERR_UNAVAILABLE;
	}
	for (BaseMaterial3D::TextureParam param : { BaseMaterial3D::TEXTURE_ALBEDO, BaseMaterial3D::TEXTURE_NORMAL, BaseMaterial3D::TEXTURE_ORM, BaseMaterial3D::TEXTURE_METALLIC, BaseMaterial3D::TEXTURE_ROUGHNESS, BaseMaterial3D::TEXTURE_AMBIENT_OCCLUSION }) {
		const Ref<Texture2D> texture = p_material->get_texture(param);
		if (texture.is_null() || (param == BaseMaterial3D::TEXTURE_NORMAL && !p_material->get_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING)) || (param == BaseMaterial3D::TEXTURE_AMBIENT_OCCLUSION && !p_material->get_feature(BaseMaterial3D::FEATURE_AMBIENT_OCCLUSION))) {
			continue;
		}
		Ref<Image> image = texture->get_image();
		if (image.is_null() || image->is_empty()) {
			return ERR_CANT_ACQUIRE_RESOURCE;
		}
		// Atlas preparation must never resize or decompress a source resource in place.
		image = image->duplicate();
		if (image->is_compressed() && image->decompress() != OK) {
			return ERR_UNAVAILABLE;
		}
		r_tile.size = r_tile.size.max(image->get_size());
		r_tile.images[param] = image;
	}
	const double scale = MIN(1.0, double(p_max_size) / r_tile.size[r_tile.size.max_axis_index()]);
	r_tile.size = Size2i(MAX(1, int(Math::round(r_tile.size.x * scale))), MAX(1, int(Math::round(r_tile.size.y * scale))));
	for (const Ref<Image> &image : r_tile.images) {
		if (image.is_valid() && image->get_size() != r_tile.size) {
			image->resize(r_tile.size.x, r_tile.size.y, Image::INTERPOLATE_LANCZOS);
		}
	}
	return OK;
}

float read_channel(const Color &p_color, BaseMaterial3D::TextureChannel p_channel) {
	return p_channel == BaseMaterial3D::TEXTURE_CHANNEL_GRAYSCALE ? (p_color.r + p_color.g + p_color.b) / 3.0f : p_color[int(p_channel)];
}

struct Atlas {
	Ref<Image> albedo;
	Ref<Image> normal;
	Ref<Image> orm;
};

void paint_tile(Tile &r_tile, const Vector2i &p_offset, Atlas &r_atlas) {
	const Ref<BaseMaterial3D> &mat = r_tile.material;
	const Color tint = mat.is_valid() ? mat->get_albedo().srgb_to_linear() : Color(1, 1, 1);
	const bool repeat = mat.is_null() || mat->get_flag(BaseMaterial3D::FLAG_USE_TEXTURE_REPEAT);
	const bool packed_orm = Object::cast_to<ORMMaterial3D>(mat.ptr()) != nullptr;
	const bool use_ao = mat.is_valid() && mat->get_feature(BaseMaterial3D::FEATURE_AMBIENT_OCCLUSION);
	const float roughness = mat.is_valid() ? mat->get_roughness() : 1.0f;
	const float metallic = mat.is_valid() ? mat->get_metallic() : 0.0f;
	const BaseMaterial3D::TextureChannel ao_channel = mat.is_valid() ? mat->get_ao_texture_channel() : BaseMaterial3D::TEXTURE_CHANNEL_RED;
	const BaseMaterial3D::TextureChannel roughness_channel = mat.is_valid() ? mat->get_roughness_texture_channel() : BaseMaterial3D::TEXTURE_CHANNEL_RED;
	const BaseMaterial3D::TextureChannel metallic_channel = mat.is_valid() ? mat->get_metallic_texture_channel() : BaseMaterial3D::TEXTURE_CHANNEL_RED;
	const Size2i atlas_size = r_atlas.albedo->get_size();
	const Size2i size = r_tile.size;
	r_tile.rect = Color(float(p_offset.x + GUTTER) / atlas_size.x, float(p_offset.y + GUTTER) / atlas_size.y, float(repeat ? size.x : -size.x) / atlas_size.x, float(size.y) / atlas_size.y);
	for (int y = -GUTTER; y < size.y + GUTTER; y++) {
		for (int x = -GUTTER; x < size.x + GUTTER; x++) {
			const int sx = repeat ? Math::posmod(x, size.x) : CLAMP(x, 0, size.x - 1);
			const int sy = repeat ? Math::posmod(y, size.y) : CLAMP(y, 0, size.y - 1);
			const Vector2i dst = p_offset + Vector2i(x + GUTTER, y + GUTTER);
			const Color albedo = r_tile.pixel(BaseMaterial3D::TEXTURE_ALBEDO, sx, sy);
			r_atlas.albedo->set_pixelv(dst, (albedo.srgb_to_linear() * tint).linear_to_srgb());
			r_atlas.normal->set_pixelv(dst, r_tile.pixel(BaseMaterial3D::TEXTURE_NORMAL, sx, sy, Color(0.5, 0.5, 1)));
			Color orm;
			if (packed_orm) {
				orm = r_tile.pixel(BaseMaterial3D::TEXTURE_ORM, sx, sy);
				if (!use_ao) {
					orm.r = 1;
				}
			} else {
				orm.r = read_channel(r_tile.pixel(BaseMaterial3D::TEXTURE_AMBIENT_OCCLUSION, sx, sy), ao_channel);
				orm.g = roughness * read_channel(r_tile.pixel(BaseMaterial3D::TEXTURE_ROUGHNESS, sx, sy), roughness_channel);
				orm.b = metallic * read_channel(r_tile.pixel(BaseMaterial3D::TEXTURE_METALLIC, sx, sy), metallic_channel);
			}
			r_atlas.orm->set_pixelv(dst, orm);
		}
	}
}

Error build_atlas(Vector<Tile> &r_tiles, Ref<ShaderMaterial> &r_material) {
	Vector<Size2i> sizes;
	uint64_t area = 0;
	for (const Tile &tile : r_tiles) {
		Size2i padded = tile.size + Vector2i(GUTTER * 2, GUTTER * 2);
		area += uint64_t(padded.x) * padded.y;
		sizes.push_back(padded);
	}
	if (area > ATLAS_LIMIT * ATLAS_LIMIT) {
		return ERR_OUT_OF_MEMORY;
	}
	Vector<Point2i> offsets;
	Size2i size;
	Geometry2D::make_atlas(sizes, offsets, size);
	if (size.x > ATLAS_LIMIT || size.y > ATLAS_LIMIT) {
		return ERR_OUT_OF_MEMORY;
	}
	Atlas atlas;
	atlas.albedo = Image::create_empty(size.x, size.y, false, Image::FORMAT_RGBA8);
	atlas.normal = Image::create_empty(size.x, size.y, false, Image::FORMAT_RGB8);
	atlas.orm = Image::create_empty(size.x, size.y, false, Image::FORMAT_RGB8);
	for (int i = 0; i < r_tiles.size(); i++) {
		paint_tile(r_tiles.write[i], offsets[i], atlas);
	}
	Ref<Shader> shader;
	shader.instantiate();
	// Store one rect per vertex so repeated UVs stay continuous across a triangle.
	shader->set_code(R"(
shader_type spatial;
uniform sampler2D albedo_atlas : source_color, filter_linear, repeat_disable;
uniform sampler2D normal_atlas : hint_normal, filter_linear, repeat_disable;
uniform sampler2D orm_atlas : filter_linear, repeat_disable;
instance uniform float hlod_fade = 1.0;
instance uniform bool hlod_fade_outgoing = false;
varying flat vec4 tile;
void vertex() {
	tile = CUSTOM0;
}
void fragment() {
	vec2 pixel = mod(floor(FRAGCOORD.xy), vec2(4.0));
	float threshold = (pixel.x + 4.0 * pixel.y + 0.5) / 16.0;
	if (hlod_fade_outgoing ? threshold < 1.0 - hlod_fade : threshold >= hlod_fade) { discard; }
	vec2 local_uv = tile.z > 0.0 ? fract(UV) : clamp(UV, vec2(0.0), vec2(1.0));
	vec2 uv = tile.xy + local_uv * abs(tile.zw);
	vec4 base_color = texture(albedo_atlas, uv) * COLOR;
	if (base_color.a < UV2.y) { discard; }
	ALBEDO = base_color.rgb;
	NORMAL_MAP = texture(normal_atlas, uv).rgb;
	NORMAL_MAP_DEPTH = UV2.x;
	vec3 orm = texture(orm_atlas, uv).rgb;
	AO = orm.r;
	ROUGHNESS = orm.g;
	METALLIC = orm.b;
}
)");
	r_material.instantiate();
	r_material->set_shader(shader);
	r_material->set_shader_parameter(SNAME("albedo_atlas"), ImageTexture::create_from_image(atlas.albedo));
	r_material->set_shader_parameter(SNAME("normal_atlas"), ImageTexture::create_from_image(atlas.normal));
	r_material->set_shader_parameter(SNAME("orm_atlas"), ImageTexture::create_from_image(atlas.orm));
	return OK;
}

Error append_surface(const SourceMesh &p_source, int p_surface, const Tile &p_tile, SurfaceTool &r_tool) {
	const Ref<Mesh> mesh = p_source.instance->get_mesh();
	const Mesh::PrimitiveType primitive = mesh->surface_get_primitive_type(p_surface);
	if (primitive != Mesh::PRIMITIVE_TRIANGLES && primitive != Mesh::PRIMITIVE_TRIANGLE_STRIP) {
		return ERR_UNAVAILABLE;
	}
	const Array arrays = mesh->surface_get_arrays(p_surface);
	if (arrays.size() != Mesh::ARRAY_MAX) {
		return ERR_INVALID_DATA;
	}
	const PackedVector3Array positions = arrays[Mesh::ARRAY_VERTEX];
	const PackedVector3Array normals = arrays[Mesh::ARRAY_NORMAL];
	const PackedVector2Array uvs = arrays[Mesh::ARRAY_TEX_UV];
	const PackedColorArray colors = arrays[Mesh::ARRAY_COLOR];
	const PackedInt32Array indices = arrays[Mesh::ARRAY_INDEX];
	const int count = indices.is_empty() ? positions.size() : indices.size();
	if (count < 3 || (primitive == Mesh::PRIMITIVE_TRIANGLES && count % 3 != 0) ||
			(!normals.is_empty() && normals.size() != positions.size()) ||
			(!uvs.is_empty() && uvs.size() != positions.size()) ||
			(!colors.is_empty() && colors.size() != positions.size())) {
		return ERR_INVALID_DATA;
	}
	const Transform3D &transform = p_source.local_to_cell;
	const Basis normal_basis = transform.basis.inverse().transposed();
	const bool mirrored = transform.basis.determinant() < 0;
	const Ref<BaseMaterial3D> &mat = p_tile.material;
	const Vector3 scale = mat.is_valid() ? mat->get_uv1_scale() : Vector3(1, 1, 1);
	const Vector3 offset = mat.is_valid() ? mat->get_uv1_offset() : Vector3();
	const bool vertex_color = !colors.is_empty() && mat.is_valid() && mat->get_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR);
	const bool srgb = vertex_color && mat->get_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR);
	const float cutout = mat.is_valid() && mat->get_transparency() == BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR ? mat->get_alpha_scissor_threshold() : 0;
	const Vector2 surface_params(mat.is_valid() ? mat->get_normal_scale() : 1.0f, cutout);
	const int step = primitive == Mesh::PRIMITIVE_TRIANGLES ? 3 : 1;
	for (int i = 0; i + 2 < count; i += step) {
		int corners[3] = { i, i + 1, i + 2 };
		if (mirrored != (step == 1 && (i % 2) == 1)) {
			SWAP(corners[1], corners[2]);
		}
		Vector3 vertices[3];
		for (int j = 0; j < 3; j++) {
			if (!indices.is_empty()) {
				corners[j] = indices[corners[j]];
			}
			if (corners[j] < 0 || corners[j] >= positions.size()) {
				return ERR_INVALID_DATA;
			}
			vertices[j] = transform.xform(positions[corners[j]]);
		}
		const Vector3 face_normal = Plane(vertices[0], vertices[1], vertices[2]).normal;
		for (int j = 0; j < 3; j++) {
			const int v = corners[j];
			Color color(1, 1, 1);
			if (vertex_color) {
				color = srgb ? colors[v].srgb_to_linear() : colors[v];
			}
			const Vector2 uv = uvs.is_empty() ? Vector2() : uvs[v];
			r_tool.set_uv(uv * Vector2(scale.x, scale.y) + Vector2(offset.x, offset.y));
			r_tool.set_uv2(surface_params);
			r_tool.set_custom(0, p_tile.rect);
			r_tool.set_color(color);
			r_tool.set_normal(normals.is_empty() ? face_normal : normal_basis.xform(normals[v]).normalized());
			r_tool.add_vertex(vertices[j]);
		}
	}
	return OK;
}

Error merge_cell(const Vector<SourceMesh> &p_sources, int p_tile_size, Ref<ArrayMesh> &r_mesh, String &r_message) {
	HashMap<Ref<Material>, int> tile_index;
	Vector<Tile> tiles;
	for (const SourceMesh &source : p_sources) {
		const Ref<Mesh> mesh = source.instance->get_mesh();
		for (int i = 0; i < mesh->get_surface_count(); i++) {
			const Ref<Material> active = source.instance->get_active_material(i);
			if (tile_index.has(active)) {
				continue;
			}
			const Ref<BaseMaterial3D> base = active;
			if (active.is_valid() && base.is_null()) {
				r_message = vformat(TTR("%s, surface %d uses a custom material. Bake UV-mapped StandardMaterial3D or ORMMaterial3D surfaces."), source.instance->get_name(), i);
				return ERR_UNAVAILABLE;
			}
			Tile tile;
			Error err = read_tile(base, p_tile_size, tile);
			if (err != OK) {
				r_message = vformat(TTR("Could not read the UV1 material maps on %s, surface %d."), source.instance->get_name(), i);
				return err;
			}
			tile_index.insert(active, tiles.size());
			tiles.push_back(tile);
		}
	}
	if (tiles.is_empty()) {
		return ERR_INVALID_DATA;
	}
	Ref<ShaderMaterial> material;
	Error err = build_atlas(tiles, material);
	if (err != OK) {
		r_message = TTR("The packed atlas exceeds 4096 pixels. Reduce the maximum tile size.");
		return err;
	}
	Ref<SurfaceTool> tool;
	tool.instantiate();
	tool->begin(Mesh::PRIMITIVE_TRIANGLES);
	tool->set_custom_format(0, SurfaceTool::CUSTOM_RGBA_FLOAT);
	tool->set_material(material);
	for (const SourceMesh &source : p_sources) {
		for (int i = 0; i < source.instance->get_mesh()->get_surface_count(); i++) {
			const int index = tile_index[source.instance->get_active_material(i)];
			err = append_surface(source, i, tiles[index], **tool);
			if (err != OK) {
				r_message = vformat(TTR("Could not read triangle geometry on %s, surface %d."), source.instance->get_name(), i);
				return err;
			}
		}
	}
	tool->generate_tangents();
	tool->index();
	r_mesh = tool->commit();
	return r_mesh.is_valid() && r_mesh->get_surface_count() == 1 ? OK : ERR_INVALID_DATA;
}

// Flatten geometry only. Runtime scripts and children belong to the authored world.
Error pack_cell(const Vector<SourceMesh> &p_sources, Ref<PackedScene> &r_scene) {
	Node3D *root = memnew(Node3D);
	root->set_name(SNAME("WorldPartitionCell"));
	for (const SourceMesh &source : p_sources) {
		MeshInstance3D *copy = memnew(MeshInstance3D);
		copy->set_name(source.instance->get_name());
		copy->set_mesh(source.instance->get_mesh());
		copy->set_material_override(source.instance->get_material_override());
		copy->set_material_overlay(source.instance->get_material_overlay());
		copy->set_layer_mask(source.instance->get_layer_mask());
		copy->set_cast_shadows_setting(source.instance->get_cast_shadows_setting());
		for (int i = 0; i < source.instance->get_surface_override_material_count(); i++) {
			copy->set_surface_override_material(i, source.instance->get_surface_override_material(i));
		}
		copy->set_transform(source.local_to_cell);
		root->add_child(copy, true);
		copy->set_owner(root);
	}
	r_scene.instantiate();
	Error err = r_scene->pack(root);
	memdelete(root);
	return err;
}

Error save_generation(const CellMap &p_cells, const Ref<WorldPartitionLayer> &p_layer, const String &p_directory, int p_tile_size, String &r_message, Vector<String> &r_outputs) {
	Ref<WorldPartitionManifest> manifest;
	manifest.instantiate();
	TypedArray<WorldPartitionLayer> layers;
	layers.push_back(p_layer);
	manifest->set_layers(layers);
	TypedArray<WorldPartitionCell> cells;
	Vector<Vector2i> coords;
	for (const KeyValue<Vector2i, Vector<SourceMesh>> &entry : p_cells) {
		coords.push_back(entry.key);
	}
	coords.sort();
	for (const Vector2i &coord : coords) {
		const String id = vformat("%s_%d_%d", p_layer->get_layer_id(), coord.x, coord.y);
		const Vector<SourceMesh> &sources = p_cells[coord];
		Ref<ArrayMesh> proxy;
		Error err = merge_cell(sources, p_tile_size, proxy, r_message);
		if (err != OK) {
			return err;
		}
		Ref<PackedScene> scene;
		err = pack_cell(sources, scene);
		if (err != OK) {
			return err;
		}
		Ref<WorldPartitionCell> cell;
		cell.instantiate();
		cell->set_layer(p_layer->get_layer_id());
		cell->set_cell_id(id);
		cell->set_grid_coords(coord);
		cell->set_hlod_impostor_path(p_directory.path_join(id + "_proxy.res"));
		cell->set_high_scene_path(p_directory.path_join(id + ".scn"));
		if (!cell->has_valid_bundles()) {
			return ERR_FILE_BAD_PATH;
		}
		r_outputs.push_back(cell->get_hlod_impostor_path());
		err = ResourceSaver::save(proxy, r_outputs[r_outputs.size() - 1], ResourceSaver::FLAG_COMPRESS);
		if (err != OK) {
			return err;
		}
		r_outputs.push_back(cell->get_high_scene_path());
		err = ResourceSaver::save(scene, r_outputs[r_outputs.size() - 1], ResourceSaver::FLAG_COMPRESS);
		if (err != OK) {
			return err;
		}
		cells.push_back(cell);
	}
	manifest->set_cells(cells);
	Error err = manifest->validate();
	if (err != OK) {
		return err;
	}
	r_outputs.push_back(p_directory.path_join("manifest.tres"));
	return ResourceSaver::save(manifest, r_outputs[r_outputs.size() - 1]);
}

} // namespace

Error SandboxHLODBaker::bake_scene(Node *p_root, const String &p_directory, const StringName &p_layer, int p_tile_size, String &r_manifest_path, String &r_message) {
	r_manifest_path = String();
	r_message = String();
	if (!p_root) {
		r_message = TTR("Open the scene containing the tagged static meshes.");
		return ERR_UNCONFIGURED;
	}
	// Validate the same project path invariants as the streaming runtime, before any I/O.
	Ref<WorldPartitionCell> path_check;
	path_check.instantiate();
	path_check->set_high_scene_path(p_directory.path_join("cell.scn"));
	if (!p_directory.begins_with("res://") || !path_check->has_valid_bundles()) {
		r_message = TTR("Choose an output directory inside res://.");
		return ERR_FILE_BAD_PATH;
	}
	if ((p_layer != SNAME("meso") && p_layer != SNAME("macro")) || p_tile_size < 1 || p_tile_size > 2048) {
		return ERR_INVALID_PARAMETER;
	}
	Ref<WorldPartitionManifest> defaults;
	defaults.instantiate();
	Ref<WorldPartitionLayer> layer;
	for (const Ref<WorldPartitionLayer> candidate : defaults->get_layers()) {
		if (candidate->get_layer_id() == p_layer) {
			layer = candidate;
			break;
		}
	}
	if (layer.is_null()) {
		return ERR_INVALID_PARAMETER;
	}
	CellMap cells;
	Error err = collect_meshes(p_root, Transform3D(), false, layer->get_cell_size(), cells, r_message);
	if (err != OK) {
		return err;
	}
	if (cells.is_empty()) {
		r_message = TTR("Add a visible MeshInstance3D or its parent to the sandbox_static group.");
		return ERR_DOES_NOT_EXIST;
	}
	err = DirAccess::make_dir_recursive_absolute(p_directory);
	if (err != OK) {
		return err;
	}
	// Reserve a fresh generation. Existing manifests keep pointing to their original cells.
	String generation;
	for (int64_t version = 1;; version++) {
		generation = p_directory.path_join(vformat("%s_%d", p_layer, version));
		if (DirAccess::dir_exists_absolute(generation)) {
			continue;
		}
		err = DirAccess::make_dir_absolute(generation);
		if (err != OK) {
			return err;
		}
		break;
	}
	Vector<String> outputs;
	err = save_generation(cells, layer, generation, p_tile_size, r_message, outputs);
	if (err != OK) {
		for (const String &path : outputs) {
			if (FileAccess::exists(path)) {
				DirAccess::remove_absolute(path);
			}
		}
		DirAccess::remove_absolute(generation);
		return err;
	}
	r_manifest_path = generation.path_join("manifest.tres");
	r_message = vformat(TTR("Baked %d cells to %s."), cells.size(), r_manifest_path);
	return OK;
}

void SandboxHLODBaker::_browse_output_directory() {
	const String current = output_directory->get_text().strip_edges();
	if (current.begins_with("res://")) {
		dir_dialog->set_current_dir(current);
	}
	dir_dialog->popup_centered_ratio(0.6);
}

void SandboxHLODBaker::_select_output_directory(const String &p_dir) {
	output_directory->set_text(p_dir);
}

void SandboxHLODBaker::_notification(int p_what) {
	if (p_what == NOTIFICATION_THEME_CHANGED && browse_directory) {
		browse_directory->set_button_icon(get_editor_theme_icon(SNAME("Folder")));
	}
}

void SandboxHLODBaker::_bake_edited_scene() {
	String path;
	String message;
	const StringName layer = grid_layer->get_selected_id() == 0 ? SNAME("meso") : SNAME("macro");
	Error err = bake_scene(EditorInterface::get_singleton()->get_edited_scene_root(), output_directory->get_text().strip_edges(), layer, tile_size->get_value(), path, message);
	result->set_text(message.is_empty() ? vformat(TTR("Bake failed (%s)."), error_names[err]) : message);
	if (err == OK) {
		EditorFileSystem::get_singleton()->scan();
	}
}

SandboxHLODBaker::SandboxHLODBaker() {
	Label *instructions = memnew(Label);
	instructions->set_text(TTR("Add static meshes or their parent to the sandbox_static group. Each bake saves a new partition manifest."));
	instructions->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	add_child(instructions);
	grid_layer = memnew(OptionButton);
	grid_layer->add_item(TTR("Meso (128 m)"), 0);
	grid_layer->add_item(TTR("Macro (512 m)"), 1);
	add_margin_child(TTR("Grid layer"), grid_layer);
	tile_size = memnew(SpinBox);
	tile_size->set_min(1);
	tile_size->set_max(2048);
	tile_size->set_value(256);
	tile_size->set_suffix(TTR("px"));
	add_margin_child(TTR("Maximum material tile size"), tile_size);
	HBoxContainer *dir_row = memnew(HBoxContainer);
	output_directory = memnew(LineEdit);
	output_directory->set_text("res://sandbox_cells");
	output_directory->set_h_size_flags(SIZE_EXPAND_FILL);
	dir_row->add_child(output_directory);
	browse_directory = memnew(Button);
	browse_directory->set_tooltip_text(TTR("Select output directory inside res://."));
	browse_directory->connect(SceneStringName(pressed), callable_mp(this, &SandboxHLODBaker::_browse_output_directory));
	dir_row->add_child(browse_directory);
	add_margin_child(TTR("Output directory"), dir_row);
	dir_dialog = memnew(EditorFileDialog);
	dir_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_DIR);
	dir_dialog->connect("dir_selected", callable_mp(this, &SandboxHLODBaker::_select_output_directory));
	add_child(dir_dialog);
	Button *bake = memnew(Button);
	bake->set_text(TTR("Bake Edited Scene"));
	bake->connect(SceneStringName(pressed), callable_mp(this, &SandboxHLODBaker::_bake_edited_scene));
	add_child(bake);
	result = memnew(Label);
	result->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	add_child(result);
}
