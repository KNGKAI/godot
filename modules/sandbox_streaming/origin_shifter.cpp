/**************************************************************************/
/*  origin_shifter.cpp                                                    */
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

#include "origin_shifter.h"

#include "core/os/thread.h"
#include "scene/3d/node_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "servers/rendering/rendering_server.h"

#include "modules/sandbox_core/event_bus.h"
#include "modules/sandbox_core/world_state.h"

OriginShifter::OriginShifter() {
	ERR_FAIL_COND(singleton);
	singleton = this;
#ifndef REAL_T_IS_DOUBLE
	if (WorldState *state = WorldState::get_singleton()) {
		state->get_world_origin_64(scene_origin[0], scene_origin[1], scene_origin[2]);
	}
#endif
}

OriginShifter::~OriginShifter() {
	singleton = nullptr;
}

PackedFloat64Array OriginShifter::get_origin_64() const {
	return { scene_origin[0], scene_origin[1], scene_origin[2] };
}

void OriginShifter::set_shift_threshold(real_t p_threshold) {
	ERR_FAIL_COND(!Math::is_finite(p_threshold) || p_threshold <= 0);
	shift_threshold = p_threshold;
}

void OriginShifter::register_root(Node3D *p_root) {
	ERR_FAIL_COND(!Thread::is_main_thread());
	ERR_FAIL_NULL(p_root);
	roots.insert(p_root->get_instance_id());
}

void OriginShifter::unregister_root(Node3D *p_root) {
	ERR_FAIL_COND(!Thread::is_main_thread());
	ERR_FAIL_NULL(p_root);
	roots.erase(p_root->get_instance_id());
}

void OriginShifter::register_invoker(Node3D *p_invoker) {
	invokers.insert(p_invoker->get_instance_id());
}

void OriginShifter::unregister_invoker(Node3D *p_invoker) {
	invokers.erase(p_invoker->get_instance_id());
}

void OriginShifter::set_multimesh_buffer(RID p_multimesh, const PackedFloat32Array &p_buffer, int p_stride) {
	ERR_FAIL_COND(!Thread::is_main_thread());
	ERR_FAIL_COND(p_multimesh.is_null());
	// 3D transform rows, followed by optional RGBA color and custom data.
	ERR_FAIL_COND(p_stride != 12 && p_stride != 16 && p_stride != 20);
	ERR_FAIL_COND(p_buffer.size() % p_stride != 0);
	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL(rs);
	rs->multimesh_set_buffer(p_multimesh, p_buffer);
#ifndef REAL_T_IS_DOUBLE
	multimeshes[p_multimesh] = { p_buffer, p_stride };
#endif
}

void OriginShifter::unregister_multimesh(RID p_multimesh) {
	ERR_FAIL_COND(!Thread::is_main_thread());
	multimeshes.erase(p_multimesh);
}

#ifndef REAL_T_IS_DOUBLE
// A registered parent moves its ordinary descendants automatically. Top-level
// nodes and Node3Ds below a plain Node have independent world transforms.
static void collect_independent_descendants(Node *p_node, HashSet<ObjectID> &r_targets) {
	for (int i = 0; i < p_node->get_child_count(true); i++) {
		Node *child = p_node->get_child(i, true);
		if (Object::cast_to<Viewport>(child)) {
			continue;
		}
		Node3D *spatial = Object::cast_to<Node3D>(child);
		if (spatial && !spatial->get_parent_node_3d()) {
			r_targets.insert(spatial->get_instance_id());
		}
		collect_independent_descendants(child, r_targets);
	}
}

void OriginShifter::_translate(const Vector3 &p_delta, Node3D *p_primary) {
	HashSet<ObjectID> targets(roots);
	HashSet<ObjectID> anchors(invokers);
	if (p_primary) {
		anchors.insert(p_primary->get_instance_id());
	}
	for (ObjectID id : anchors) {
		Node3D *anchor = ObjectDB::get_instance<Node3D>(id);
		if (!anchor || !anchor->is_inside_tree()) {
			continue;
		}
		// For an invoker attached to a player or camera, move its rig as a
		// unit. Respect an explicitly registered root within the rig.
		Node3D *root = anchor;
		while (!roots.has(root->get_instance_id()) && root->get_parent_node_3d()) {
			root = root->get_parent_node_3d();
		}
		targets.insert(root->get_instance_id());
	}

	HashSet<ObjectID> independent;
	for (ObjectID id : targets) {
		Node3D *root = ObjectDB::get_instance<Node3D>(id);
		if (!root) {
			roots.erase(id);
			continue;
		}
		if (!root->is_inside_tree()) {
			continue;
		}
		Node3D *ancestor = root->get_parent_node_3d();
		while (ancestor && !targets.has(ancestor->get_instance_id())) {
			ancestor = ancestor->get_parent_node_3d();
		}
		if (!ancestor) {
			independent.insert(id);
			collect_independent_descendants(root, independent);
		}
	}
	for (ObjectID id : independent) {
		Node3D *root = ObjectDB::get_instance<Node3D>(id);
		if (root && root->is_inside_tree()) {
			root->set_global_position(root->get_global_position() - p_delta);
		}
	}
	// CollisionObject3D forwards transform notifications to the physics
	// server. Flush before its upcoming sync, including when FTI is disabled.
	if (SceneTree *tree = SceneTree::get_singleton()) {
		tree->flush_transform_notifications();
	}
	for (ObjectID id : independent) {
		Node3D *root = ObjectDB::get_instance<Node3D>(id);
		if (root && root->is_inside_tree()) {
			root->reset_physics_interpolation();
		}
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	for (KeyValue<RID, ProxyBuffer> &entry : multimeshes) {
		ProxyBuffer &buffer = entry.value;
		float *data = buffer.data.ptrw();
		for (int64_t offset = 0; offset < buffer.data.size(); offset += buffer.stride) {
			data[offset + 3] -= p_delta.x;
			data[offset + 7] -= p_delta.y;
			data[offset + 11] -= p_delta.z;
		}
		rs->multimesh_set_buffer(entry.key, buffer.data);
		rs->multimesh_instances_reset_physics_interpolation(entry.key);
	}
	if (EventBus *events = EventBus::get_singleton()) {
		Dictionary payload;
		payload["delta"] = p_delta;
		payload["new_origin_64"] = get_origin_64();
		events->emit(SNAME("origin_shifted"), payload);
	}
}
#endif

void OriginShifter::sync_from_world_state() {
	ERR_FAIL_COND(!Thread::is_main_thread());
	WorldState *state = WorldState::get_singleton();
	ERR_FAIL_NULL(state);
#ifdef REAL_T_IS_DOUBLE
	// A single-precision save may carry a nonzero origin. Native LWC
	// projects use the absolute world coordinates already in the snapshot.
	double x, y, z;
	state->get_world_origin_64(x, y, z);
	if (x != 0.0 || y != 0.0 || z != 0.0) {
		state->set_world_origin_64(0, 0, 0);
	}
#else
	double restored[3];
	state->get_world_origin_64(restored[0], restored[1], restored[2]);
	Vector3 delta;
	for (int i = 0; i < 3; i++) {
		delta[i] = restored[i] - scene_origin[i];
	}
	ERR_FAIL_COND(!delta.is_finite());
	if (delta != Vector3()) {
		for (int i = 0; i < 3; i++) {
			scene_origin[i] = restored[i];
		}
		_translate(delta, nullptr);
	}
#endif
}

bool OriginShifter::check_and_shift(Node3D *p_primary) {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), false);
#ifdef REAL_T_IS_DOUBLE
	return false;
#else
	const ObjectID primary_id = p_primary ? p_primary->get_instance_id() : ObjectID();
	sync_from_world_state();
	Node3D *primary = ObjectDB::get_instance<Node3D>(primary_id);
	if (!primary || !primary->is_inside_tree()) {
		return false;
	}
	const Vector3 local = primary->get_global_position();
	if (!local.is_finite() || local.length() <= shift_threshold) {
		return false;
	}
	const Vector3 delta = local.snappedf(MIN((real_t)512.0, shift_threshold));
	for (int i = 0; i < 3; i++) {
		scene_origin[i] += delta[i];
	}
	WorldState::get_singleton()->set_world_origin_64(scene_origin[0], scene_origin[1], scene_origin[2]);
	_translate(delta, primary);
	return true;
#endif
}

void OriginShifter::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_origin_64"), &OriginShifter::get_origin_64);
	ClassDB::bind_method(D_METHOD("set_shift_threshold", "threshold"), &OriginShifter::set_shift_threshold);
	ClassDB::bind_method(D_METHOD("get_shift_threshold"), &OriginShifter::get_shift_threshold);
	ClassDB::bind_method(D_METHOD("register_root", "root"), &OriginShifter::register_root);
	ClassDB::bind_method(D_METHOD("unregister_root", "root"), &OriginShifter::unregister_root);
	ClassDB::bind_method(D_METHOD("set_multimesh_buffer", "multimesh", "buffer", "stride"), &OriginShifter::set_multimesh_buffer, DEFVAL(12));
	ClassDB::bind_method(D_METHOD("unregister_multimesh", "multimesh"), &OriginShifter::unregister_multimesh);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shift_threshold", PROPERTY_HINT_RANGE, "1,16384,1,or_greater,suffix:m"), "set_shift_threshold", "get_shift_threshold");
}
