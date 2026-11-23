/**************************************************************************/
/*  cell_residency.cpp                                                    */
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

#include "cell_residency.h"

#include "origin_shifter.h"

#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/3d/shape_3d.h"

void CellSceneBundle::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_batches", "batches"), &CellSceneBundle::set_batches);
	ClassDB::bind_method(D_METHOD("get_batches"), &CellSceneBundle::get_batches);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "batches", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("PackedScene")), "set_batches", "get_batches");
}

namespace {
bool has_time(uint64_t p_deadline_usec) {
	return OS::get_singleton()->get_ticks_usec() < p_deadline_usec;
}

void retire(ObjectID p_id) {
	Node *node = ObjectDB::get_instance<Node>(p_id);
	if (!node) {
		return;
	}
	// Leaving the world unregisters collision objects and visual instances
	// immediately. The tree releases their mesh/shape resources on deletion.
	SceneTree *tree = node->is_inside_tree() ? node->get_tree() : nullptr;
	if (node->get_parent()) {
		node->get_parent()->remove_child(node);
	}
	node = ObjectDB::get_instance<Node>(p_id);
	if (!node || node->is_queued_for_deletion()) {
		return;
	}
	if (tree) {
		tree->queue_delete(node);
	} else {
		memdelete(node);
	}
}

bool retire_batches(ObjectID &r_group, uint64_t p_deadline_usec) {
	while (r_group.is_valid()) {
		Node *group = ObjectDB::get_instance<Node>(r_group);
		if (!group) {
			r_group = ObjectID();
			break;
		}
		if (!has_time(p_deadline_usec)) {
			return false;
		}
		const int children = group->get_child_count(true);
		if (children == 0) {
			retire(r_group);
			r_group = ObjectID();
		} else {
			retire(group->get_child(children - 1, true)->get_instance_id());
		}
	}
	return true;
}

Node3D *instantiate_batch(const Ref<Resource> &p_resource, bool p_visual) {
	Ref<PackedScene> scene = p_resource;
	if (scene.is_valid()) {
		Node *node = scene->instantiate();
		Node3D *spatial = Object::cast_to<Node3D>(node);
		if (node && !spatial) {
			memdelete(node);
		}
		return spatial;
	}
	if (p_visual) {
		Ref<Mesh> mesh = p_resource;
		if (mesh.is_valid()) {
			MeshInstance3D *instance = memnew(MeshInstance3D);
			instance->set_mesh(mesh);
			return instance;
		}
	} else {
		Ref<Shape3D> shape = p_resource;
		if (shape.is_valid()) {
			StaticBody3D *body = memnew(StaticBody3D);
			CollisionShape3D *collider = memnew(CollisionShape3D);
			collider->set_shape(shape);
			body->add_child(collider);
			return body;
		}
	}
	return nullptr;
}
} // namespace

Error CellResidency::Asset::start(const String &p_path) {
	if (p_path.is_empty()) {
		return OK;
	}
	Error err = ResourceLoader::load_threaded_request(p_path, "", false);
	if (err == OK) {
		request = p_path;
	}
	return err;
}

Error CellResidency::Asset::poll() {
	if (request.is_empty()) {
		return OK;
	}
	if (ResourceLoader::load_threaded_get_status(request) == ResourceLoader::THREAD_LOAD_IN_PROGRESS) {
		return ERR_BUSY;
	}
	Error err;
	resource = ResourceLoader::load_threaded_get(request, &err);
	request = String();
	return err == OK && resource.is_null() ? ERR_CANT_ACQUIRE_RESOURCE : err;
}

void CellResidency::drain_requests(bool p_wait) {
	for (int i = abandoned_requests.size() - 1; i >= 0; --i) {
		const String path = abandoned_requests[i];
		if (p_wait || ResourceLoader::load_threaded_get_status(path) != ResourceLoader::THREAD_LOAD_IN_PROGRESS) {
			ResourceLoader::load_threaded_get(path);
			abandoned_requests.remove_at(i);
		}
	}
}

void CellResidency::discard_pending() {
	for (Asset *asset : { &collision_asset, &visual_asset }) {
		if (!asset->request.is_empty()) {
			abandoned_requests.push_back(asset->request);
		}
		retire(asset->staged_batch);
		*asset = Asset();
	}
	retire(pending_collision);
	retire(pending.root);
	pending_collision = ObjectID();
	pending = Visual();
	scan_stack.clear();
	hidden_batches.clear();
	reveal_index = 0;
	pending_lod = LOD_NONE;
}

void CellResidency::retire_anchor() {
	Node3D *anchor = get_root();
	if (anchor && OriginShifter::get_singleton()) {
		OriginShifter::get_singleton()->unregister_root(anchor);
	}
	retire(root);
	root = ObjectID();
}

void CellResidency::fail(Error p_error) {
	failed_lod = pending_lod;
	last_error = p_error;
	discard_pending();
	if (active_lod == LOD_NONE) {
		retire_anchor();
		state = abandoned_requests.is_empty() ? UNLOADED : EVICTING;
	} else {
		state = ACTIVE_ATTACHED;
	}
}

void CellResidency::scan_geometry(uint64_t p_deadline_usec) {
	while (!scan_stack.is_empty() && has_time(p_deadline_usec)) {
		ScanCursor &cursor = scan_stack.write[scan_stack.size() - 1];
		Node *node = ObjectDB::get_instance<Node>(cursor.node);
		if (node && cursor.next_child == -1) {
			if (GeometryInstance3D *geometry = Object::cast_to<GeometryInstance3D>(node)) {
				geometry->set_instance_shader_parameter(SNAME("hlod_fade_outgoing"), false);
				geometry->set_instance_shader_parameter(SNAME("hlod_fade"), current.root.is_valid() ? 0.0f : 1.0f);
				pending.geometry.push_back(cursor.node);
			}
			cursor.next_child = 0;
		}
		if (node && cursor.next_child < node->get_child_count(true)) {
			Node *child = node->get_child(cursor.next_child++, true);
			scan_stack.push_back({ child->get_instance_id() });
		} else {
			scan_stack.resize(scan_stack.size() - 1);
		}
	}
}

bool CellResidency::SceneStep::try_begin() {
	if (taken || !has_time(start_deadline_usec)) {
		return false;
	}
	taken = true;
	return true;
}

Error CellResidency::attach_batches(Asset &r_asset, ObjectID &r_group, bool p_visual, uint64_t p_deadline_usec, SceneStep *p_step) {
	if (r_asset.resource.is_null()) {
		return OK;
	}
	const Ref<CellSceneBundle> bundle = r_asset.resource;
	const TypedArray<PackedScene> batches = bundle.is_valid() ? bundle->get_batches() : TypedArray<PackedScene>();
	const int count = bundle.is_valid() ? batches.size() : 1;
	if (count == 0) {
		return ERR_INVALID_DATA;
	}
	while (has_time(p_deadline_usec)) {
		if (p_visual) {
			scan_geometry(p_deadline_usec);
			if (!scan_stack.is_empty() || !has_time(p_deadline_usec)) {
				break;
			}
		}
		if (r_asset.next_batch == count) {
			r_asset.resource.unref();
			return OK;
		}
		if (p_step && !p_step->try_begin()) {
			return ERR_BUSY;
		}
		if (r_asset.staged_batch.is_null()) {
			Ref<Resource> batch = r_asset.resource;
			if (bundle.is_valid()) {
				batch = batches[r_asset.next_batch];
			}
			Node3D *instance = instantiate_batch(batch, p_visual);
			if (!instance) {
				return ERR_INVALID_DATA;
			}
			r_asset.staged_batch = instance->get_instance_id();
			continue;
		}
		Node3D *node = ObjectDB::get_instance<Node3D>(r_asset.staged_batch);
		Node3D *anchor = get_root();
		if (!node || !anchor || anchor->is_queued_for_deletion()) {
			return ERR_INVALID_DATA;
		}
		Node3D *group = ObjectDB::get_instance<Node3D>(r_group);
		if (!group) {
			if (r_group.is_valid()) {
				return ERR_INVALID_DATA;
			}
			group = memnew(Node3D);
			r_group = group->get_instance_id();
			anchor->add_child(group);
		}
		group = ObjectDB::get_instance<Node3D>(r_group);
		node = ObjectDB::get_instance<Node3D>(r_asset.staged_batch);
		if (!group || group->is_queued_for_deletion() || !node) {
			return ERR_INVALID_DATA;
		}
		const ObjectID instance_id = node->get_instance_id();
		if (p_visual) {
			// Keep the authored node's visibility and any _ready changes intact.
			Node3D *wrapper = memnew(Node3D);
			wrapper->set_visible(false);
			wrapper->add_child(node);
			hidden_batches.push_back(wrapper->get_instance_id());
			group->add_child(wrapper);
		} else {
			group->add_child(node);
		}
		r_asset.staged_batch = ObjectID();
		++r_asset.next_batch;
		if (p_visual) {
			scan_stack.push_back({ instance_id });
		}
	}
	return ERR_BUSY;
}

void CellResidency::advance_fade(double p_delta, uint64_t p_deadline_usec) {
	if (outgoing.root.is_null()) {
		return;
	}
	constexpr double fade_seconds = 0.25;
	fade_elapsed = MIN(fade_seconds, fade_elapsed + MAX(0.0, p_delta));
	if (fade_index == 0) {
		fade_sample = fade_elapsed / fade_seconds;
	}
	// Bring the new geometry up before reducing the old geometry. A frame
	// deadline may interrupt either pass without opening a hole in the cell.
	const int count = current.geometry.size() + outgoing.geometry.size();
	while (fade_index < count && has_time(p_deadline_usec)) {
		bool incoming = fade_index < current.geometry.size();
		ObjectID id = incoming ? current.geometry[fade_index] : outgoing.geometry[fade_index - current.geometry.size()];
		if (GeometryInstance3D *geometry = ObjectDB::get_instance<GeometryInstance3D>(id)) {
			if (!incoming) {
				// Complement the incoming screen-door mask in the host shader.
				geometry->set_instance_shader_parameter(SNAME("hlod_fade_outgoing"), true);
			}
			geometry->set_instance_shader_parameter(SNAME("hlod_fade"), incoming ? fade_sample : 1.0f - fade_sample);
		}
		++fade_index;
	}
	if (fade_index == count) {
		if (fade_sample >= 1.0f) {
			if (retire_batches(outgoing.root, p_deadline_usec)) {
				outgoing = Visual();
			}
		} else {
			fade_index = 0;
		}
	}
}

bool CellResidency::tick(Lod p_target, const Paths &p_paths, Node3D *p_parent, const Vector3 &p_local_position, double p_delta, uint64_t p_deadline_usec, SceneStep *p_step) {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), false);
	const ObjectID parent_id = p_parent ? p_parent->get_instance_id() : ObjectID();
	drain_requests(false);
	if (root.is_valid() && !get_root()) {
		// Scene teardown may have already freed all attached representations.
		discard_pending();
		current = Visual();
		outgoing = Visual();
		active_lod = LOD_NONE;
		root = ObjectID();
		collision = ObjectID();
		state = UNLOADED;
	}
	if (p_target != failed_lod) {
		failed_lod = LOD_NONE;
		last_error = OK;
	}
	if (p_target == LOD_NONE || state == EVICTING) {
		if (pending_lod == LOD_NONE && active_lod == LOD_NONE && root.is_null()) {
			state = abandoned_requests.is_empty() ? UNLOADED : EVICTING;
			if (p_target == LOD_NONE || state == EVICTING) {
				return false;
			}
		} else {
			state = EVICTING;
			// Each authored subtree is one atomic SceneTree exit. Remove
			// visuals first so colliders outlive the visible representation.
			for (ObjectID *group : { &pending.root, &outgoing.root, &current.root, &pending_collision, &collision }) {
				if (!retire_batches(*group, p_deadline_usec)) {
					return false;
				}
			}
			if (!has_time(p_deadline_usec)) {
				return false;
			}
			discard_pending();
			retire_anchor();
			current = Visual();
			outgoing = Visual();
			active_lod = LOD_NONE;
			return false;
		}
	}
	if (active_lod == LOD_PROXY && !retire_batches(collision, p_deadline_usec)) {
		return false;
	}
	advance_fade(p_delta, p_deadline_usec);
	p_parent = ObjectDB::get_instance<Node3D>(parent_id);
	if (outgoing.root.is_valid() || p_target == failed_lod || !p_parent || p_parent->is_queued_for_deletion()) {
		return false;
	}
	if (pending_lod != LOD_NONE && p_target != pending_lod) {
		if (!has_time(p_deadline_usec)) {
			return false;
		}
		discard_pending();
		state = active_lod == LOD_NONE ? UNLOADED : ACTIVE_ATTACHED;
	}
	if (active_lod == p_target) {
		return false;
	}
	if (pending_lod == LOD_NONE) {
		if (!has_time(p_deadline_usec)) {
			return false;
		}
		pending_lod = p_target;
		// A cell may consist of one proxy mesh and a near-field collision bundle.
		// Retain that mesh when the high tier adds collision.
		const String &high_path = p_paths.high.is_empty() ? p_paths.proxy : p_paths.high;
		const String &visual_path = p_target == LOD_HIGH ? high_path : (p_target == LOD_MID ? p_paths.mid : p_paths.proxy);
		const bool needs_collision = p_target != LOD_PROXY && !p_paths.collision.is_empty();
		if (visual_path.is_empty() && !needs_collision) {
			fail(ERR_INVALID_DATA);
			return false;
		}
		Error err = OK;
		if (needs_collision && collision.is_null()) {
			err = collision_asset.start(p_paths.collision);
		}
		if (err == OK) {
			err = visual_asset.start(visual_path);
		}
		if (err != OK) {
			fail(err);
			return false;
		}
		state = LOADING_ASYNC;
		return false;
	}

	for (Asset *asset : { &collision_asset, &visual_asset }) {
		Error err = asset->poll();
		if (err == ERR_BUSY) {
			return false;
		}
		if (err != OK) {
			fail(err);
			return false;
		}
		if (asset->resource.is_null()) {
			continue;
		}
		state = STAGED;
		if (!has_time(p_deadline_usec)) {
			return false;
		}
		if (root.is_null()) {
			p_parent = ObjectDB::get_instance<Node3D>(parent_id);
			if (!p_parent || p_parent->is_queued_for_deletion()) {
				return false;
			}
			Node3D *anchor = memnew(Node3D);
			root = anchor->get_instance_id();
			anchor->set_as_top_level(true);
			anchor->set_position(p_local_position);
			if (OriginShifter::get_singleton()) {
				OriginShifter::get_singleton()->register_root(anchor);
			}
			p_parent->add_child(anchor);
		}
		const bool visual = asset == &visual_asset;
		err = attach_batches(*asset, visual ? pending.root : pending_collision, visual, p_deadline_usec, p_step);
		if (err != OK) {
			if (err != ERR_BUSY) {
				fail(err);
			}
			return false;
		}
	}
	// Visibility notifications propagate recursively. Reveal one authored
	// subtree at a time before starting the cross-fade.
	while (reveal_index < hidden_batches.size()) {
		if (!has_time(p_deadline_usec)) {
			return false;
		}
		if (Node3D *batch = ObjectDB::get_instance<Node3D>(hidden_batches[reveal_index])) {
			batch->set_visible(true);
		}
		++reveal_index;
	}
	hidden_batches.clear();
	reveal_index = 0;
	Node3D *anchor = get_root();
	if (!anchor || anchor->is_queued_for_deletion() || (pending.root.is_valid() && !ObjectDB::get_instance(pending.root))) {
		fail(ERR_INVALID_DATA);
		return false;
	}
	if (p_target == LOD_PROXY) {
		retire_batches(collision, p_deadline_usec);
	} else if (pending_collision.is_valid()) {
		collision = pending_collision;
	}
	pending_collision = ObjectID();
	outgoing = std::move(current);
	current = std::move(pending);
	pending = Visual();
	fade_elapsed = 0.0;
	fade_index = 0;
	collision_asset = Asset();
	visual_asset = Asset();
	active_lod = pending_lod;
	pending_lod = LOD_NONE;
	state = ACTIVE_ATTACHED;
	return true;
}

Node3D *CellResidency::get_root() const {
	return ObjectDB::get_instance<Node3D>(root);
}

void CellResidency::clear() {
	discard_pending();
	retire_anchor();
	collision = ObjectID();
	current = Visual();
	outgoing = Visual();
	active_lod = LOD_NONE;
	failed_lod = LOD_NONE;
	last_error = OK;
	state = UNLOADED;
	drain_requests(true);
}

CellResidency::~CellResidency() {
	clear();
}
