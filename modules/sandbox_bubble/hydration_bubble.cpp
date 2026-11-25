/**************************************************************************/
/*  hydration_bubble.cpp                                                  */
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

#include "hydration_bubble.h"

#include "actor_node_pool.h"
#include "hydratable_actor.h"
#include "hydration_proxy_batch.h"

#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/world_3d.h"

#include "modules/sandbox_core/command_bus.h"
#include "modules/sandbox_core/sandbox_clock.h"
#include "modules/sandbox_core/sandbox_serializer.h"
#include "modules/sandbox_sim/simulation_server.h"
#include "modules/sandbox_streaming/origin_shifter.h"
#include "modules/sandbox_streaming/streaming_invoker.h"
#include "modules/sandbox_streaming/streaming_server.h"

struct HydrationBubble::Archetype {
	ActorNodePool pool;
	HydrationProxyBatch proxies;
	Ref<Mesh> mesh;
	StringName name;
	int capacity = 0;
	int overflow_policy = KEEP_TIER1_PROXY;
	double enter = 140;
	double exit = 165;
	double proxy = 400;
};

bool HydrationBubble::Candidate::operator<(const Candidate &p_other) const {
	if (pinned != p_other.pinned) {
		return pinned;
	}
	if (visible != p_other.visible) {
		return visible;
	}
	return distance_squared == p_other.distance_squared ? rid < p_other.rid : distance_squared < p_other.distance_squared;
}

Error HydrationBubble::configure_archetype(const StringName &p_type, const Ref<PackedScene> &p_scene, int p_capacity, const Ref<Mesh> &p_proxy_mesh, int p_overflow_policy) {
	ERR_FAIL_COND_V(!Thread::is_main_thread() || busy || host.is_valid(), ERR_BUSY);
	ERR_FAIL_COND_V(archetypes.has(p_type), ERR_ALREADY_IN_USE);
	ERR_FAIL_COND_V(p_overflow_policy < KEEP_TIER1_PROXY || p_overflow_policy > RECYCLE_FARTHEST_UNPINNED, ERR_INVALID_PARAMETER);
	Archetype *type = memnew(Archetype);
	Error err = type->pool.prewarm(p_scene, p_capacity, p_type);
	if (err != OK) {
		memdelete(type);
		return err;
	}
	Node3D *sample = type->pool.claim();
	HydratableActor *component = type->pool.get_component(sample);
	type->enter = component->get_hydrate_radius();
	type->exit = component->get_dehydrate_radius();
	type->proxy = MAX(type->proxy, type->exit);
	type->pool.release(sample);
	type->name = p_type;
	type->mesh = p_proxy_mesh;
	type->capacity = p_capacity;
	type->overflow_policy = p_overflow_policy;
	archetypes.insert(p_type, type);
	return OK;
}

Error HydrationBubble::set_radii(const StringName &p_type, double p_enter, double p_exit, double p_proxy) {
	ERR_FAIL_COND_V(!Thread::is_main_thread() || busy, ERR_BUSY);
	Archetype **type = archetypes.getptr(p_type);
	if (!type || !Math::is_finite(p_enter) || !Math::is_finite(p_exit) || !Math::is_finite(p_proxy) || p_enter <= 0 || p_exit <= p_enter || p_proxy < p_exit) {
		return ERR_INVALID_PARAMETER;
	}
	(*type)->enter = p_enter;
	(*type)->exit = p_exit;
	(*type)->proxy = p_proxy;
	return OK;
}

void HydrationBubble::_set_hooks(bool p_enabled) {
	if (SandboxClock *clock = SandboxClock::get_singleton()) {
		clock->set_hydration_update_callback(p_enabled ? +[](double p_delta) { singleton->update(p_delta); } : nullptr);
	}
	if (SandboxSerializer *serializer = SandboxSerializer::get_singleton()) {
		serializer->set_pre_save_flush_callback(p_enabled ? +[]() { singleton->flush_active_actors_to_soa(); } : nullptr);
		serializer->set_post_load_resync_callback(p_enabled ? +[]() { singleton->resync_active_actors_from_soa(); } : nullptr);
	}
}

Error HydrationBubble::start(Node3D *p_parent) {
	ERR_FAIL_COND_V(!Thread::is_main_thread() || busy || host.is_valid(), ERR_BUSY);
	ERR_FAIL_COND_V(!p_parent || !p_parent->is_inside_tree() || p_parent->is_queued_for_deletion(), ERR_INVALID_PARAMETER);
	host = p_parent->get_instance_id();
	p_parent->connect(SNAME("tree_exiting"), callable_mp(this, &HydrationBubble::_on_host_exiting));
	for (const KeyValue<StringName, Archetype *> &entry : archetypes) {
		entry.value->proxies.configure(entry.value->mesh, p_parent->get_world_3d()->get_scenario());
	}
	hydrations = 0;
	dehydrations = 0;
	last_transition_usec = 0;
	last_update_usec = 0;
	last_proxy_usec = 0;
	CommandBus *bus = CommandBus::get_singleton();
	bus->register_command_native(SNAME("bubble.stats"), CommandBus::PERMISSION_USER, {}, callable_mp(this, &HydrationBubble::get_stats));
	bus->register_command_native(SNAME("bubble.radius"), CommandBus::PERMISSION_ADMIN, { Variant::STRING, Variant::FLOAT, Variant::FLOAT }, callable_mp(this, &HydrationBubble::_cmd_radius));
	bus->register_command_native(SNAME("bubble.pin"), CommandBus::PERMISSION_ADMIN, { Variant::INT }, callable_mp(this, &HydrationBubble::_cmd_pin));
	bus->register_command_native(SNAME("bubble.unpin"), CommandBus::PERMISSION_ADMIN, { Variant::INT }, callable_mp(this, &HydrationBubble::_cmd_unpin));
	_set_hooks(true);
	return OK;
}

void HydrationBubble::_on_host_exiting() {
	stop();
}

void HydrationBubble::stop() {
	ERR_FAIL_COND(!Thread::is_main_thread());
	if (busy) {
		callable_mp(this, &HydrationBubble::stop).call_deferred();
		return;
	}
	busy = true;
	if (host.is_valid()) {
		if (CommandBus *bus = CommandBus::get_singleton()) {
			for (const char *name : { "bubble.stats", "bubble.radius", "bubble.pin", "bubble.unpin" }) {
				bus->unregister_command(name);
			}
		}
		_set_hooks(false);
		if (Node3D *parent = ObjectDB::get_instance<Node3D>(host)) {
			parent->disconnect(SNAME("tree_exiting"), callable_mp(this, &HydrationBubble::_on_host_exiting));
		}
		while (!active.is_empty()) {
			_release(active.begin()->key, true);
		}
		SimulationServer *sim = SimulationServer::get_singleton();
		if (sim) {
			for (uint32_t i = 0; i < sim->get_agent_count(); i++) {
				sim->get_agent_hydration_tier_ptr()[i] = SimulationServer::TIER_0_ABSTRACT_SOA;
			}
		}
		host = ObjectID();
	}
	for (const KeyValue<StringName, Archetype *> &entry : archetypes) {
		memdelete(entry.value);
	}
	archetypes.clear();
	busy = false;
}

Dictionary HydrationBubble::_read_state(RID p_rid) const {
	SimulationServer *sim = SimulationServer::get_singleton();
	Dictionary state;
	state["velocity"] = sim->get_agent_velocity(p_rid);
	state["health"] = sim->get_agent_health(p_rid);
	state["state_flags"] = sim->get_agent_state_flags(p_rid);
	state["spline_cursor"] = sim->get_agent_spline_cursor(p_rid);
	state["custom_blob"] = sim->get_agent_custom_blob(p_rid);
	return state;
}

void HydrationBubble::_write_state(RID p_rid, const Dictionary &p_state, const Transform3D &p_transform) {
	SimulationServer *sim = SimulationServer::get_singleton();
	if (!sim->has_agent(p_rid)) {
		return;
	}
	const PackedFloat64Array origin = OriginShifter::get_singleton()->get_origin_64();
	double position[3];
	sim->get_agent_position_64(p_rid, position[0], position[1], position[2]);
	for (int axis = 0; axis < 3; axis++) {
		// Preserve the original double when a stationary shell is flushed through
		// a single-precision scene transform.
		if (real_t(position[axis] - origin[axis]) != p_transform.origin[axis]) {
			position[axis] = origin[axis] + p_transform.origin[axis];
		}
	}
	sim->set_agent_position_64(p_rid, position[0], position[1], position[2]);
	sim->set_agent_rotation_y(p_rid, p_transform.basis.get_euler().y);
	if (p_state.has("velocity")) {
		sim->set_agent_velocity(p_rid, p_state["velocity"]);
	}
	if (p_state.has("health")) {
		sim->set_agent_health(p_rid, p_state["health"]);
	}
	if (p_state.has("state_flags")) {
		sim->set_agent_state_flags(p_rid, p_state["state_flags"]);
	}
	if (p_state.has("spline_cursor")) {
		sim->set_agent_spline_cursor(p_rid, p_state["spline_cursor"]);
	}
	if (p_state.has("custom_blob")) {
		sim->set_agent_custom_blob(p_rid, p_state["custom_blob"]);
	}
}

Node3D *HydrationBubble::get_active_actor(RID p_rid) const {
	const ActiveActor *actor = active.getptr(p_rid);
	return actor ? ObjectDB::get_instance<Node3D>(actor->root) : nullptr;
}

void HydrationBubble::_restore_actor(RID p_rid) {
	SimulationServer *sim = SimulationServer::get_singleton();
	Node3D *root = get_active_actor(p_rid);
	HydratableActor *component = active[p_rid].type->pool.get_component(root);
	if (!component) {
		return;
	}
	Transform3D transform{ Basis(Vector3(0, 1, 0), sim->get_agent_rotation_y(p_rid)) };
	double x, y, z;
	sim->get_agent_position_64(p_rid, x, y, z);
	const PackedFloat64Array origin = OriginShifter::get_singleton()->get_origin_64();
	transform.origin = Vector3(x - origin[0], y - origin[1], z - origin[2]);
	root->set_global_transform(transform);
	root->reset_physics_interpolation();
	sim->set_agent_hydration_tier(p_rid, SimulationServer::TIER_2_HYDRATED_NODE);
	component->hydrate(p_rid, _read_state(p_rid));
}

void HydrationBubble::_hydrate(RID p_rid, Archetype *p_type, Node3D *p_parent) {
	Node3D *root = p_type->pool.claim();
	if (!root) {
		return;
	}
	const ObjectID root_id = root->get_instance_id();
	const Node::ProcessMode mode = root->get_process_mode();
	root->set_process_mode(Node::PROCESS_MODE_DISABLED);
	active.insert(p_rid, { root_id, p_type });
	p_parent->add_child(root);
	if (!ObjectDB::get_instance<Node3D>(root_id) || !SimulationServer::get_singleton()->has_agent(p_rid)) {
		_release(p_rid, false);
		return;
	}
	OriginShifter::get_singleton()->register_root(root);
	_restore_actor(p_rid);
	if (Node3D *actor = ObjectDB::get_instance<Node3D>(root_id)) {
		actor->set_process_mode(mode);
	}
	hydrations++;
}

void HydrationBubble::_release(RID p_rid, bool p_write_back) {
	ActiveActor actor = active[p_rid];
	Node3D *root = ObjectDB::get_instance<Node3D>(actor.root);
	active.erase(p_rid);
	if (root) {
		OriginShifter::get_singleton()->unregister_root(root);
		HydratableActor *component = actor.type->pool.get_component(root);
		if (component && component->get_bound_rid() == p_rid) {
			Transform3D transform = root->get_transform();
			if (root->is_inside_tree()) {
				transform = root->get_global_transform();
			} else if (Node3D *parent = Object::cast_to<Node3D>(root->get_parent()); parent && parent->is_inside_tree() && !root->is_set_as_top_level()) {
				transform = parent->get_global_transform() * transform;
			}
			Dictionary state = component->dehydrate();
			if (p_write_back) {
				_write_state(p_rid, state, transform);
			}
		}
		if (Node3D *shell = ObjectDB::get_instance<Node3D>(actor.root)) {
			actor.type->pool.release(shell);
		}
	}
	SimulationServer *sim = SimulationServer::get_singleton();
	if (sim && sim->has_agent(p_rid)) {
		sim->set_agent_hydration_tier(p_rid, SimulationServer::TIER_0_ABSTRACT_SOA);
	}
	dehydrations++;
}

void HydrationBubble::flush_active_actors_to_soa() {
	ERR_FAIL_COND(!Thread::is_main_thread());
	if (flushing) {
		return;
	}
	const bool already_busy = busy;
	busy = true;
	flushing = true;
	for (const KeyValue<RID, ActiveActor> &entry : active) {
		Node3D *root = get_active_actor(entry.key);
		if (root && root->is_inside_tree()) {
			if (HydratableActor *component = entry.value.type->pool.get_component(root)) {
				const Transform3D transform = root->get_global_transform();
				_write_state(entry.key, component->capture_state(), transform);
			}
		}
	}
	flushing = false;
	busy = already_busy;
}

void HydrationBubble::resync_active_actors_from_soa() {
	ERR_FAIL_COND(!Thread::is_main_thread() || busy);
	busy = true;
	OriginShifter::get_singleton()->sync_from_world_state();
	SimulationServer *sim = SimulationServer::get_singleton();
	LocalVector<RID> stale;
	for (const KeyValue<RID, ActiveActor> &entry : active) {
		if (sim->has_agent(entry.key) && sim->get_agent_archetype(entry.key) == entry.value.type->name && get_active_actor(entry.key)) {
			_restore_actor(entry.key);
		} else {
			stale.push_back(entry.key);
		}
	}
	for (RID rid : stale) {
		_release(rid, false);
	}
	busy = false;
}
namespace {
struct BubbleInvoker {
	Vector3 center;
	Vector3 position;
	Vector3 forward;
	Vector3 velocity_axis;
	real_t lead = 0;
	Vector<Plane> frustum;

	double distance_squared(const Vector3 &p_position, double p_radius) const {
		const Vector3 offset = p_position - center;
		const double axial = offset.dot(velocity_axis);
		const double scale = p_radius / (p_radius + lead);
		return MAX(0.0, double(offset.length_squared()) - axial * axial * (1.0 - scale * scale));
	}

	bool sees(const Vector3 &p_position) const {
		if (frustum.is_empty()) {
			return forward.dot(p_position - position) >= 0;
		}
		for (const Plane &plane : frustum) {
			if (plane.is_point_over(p_position)) {
				return false;
			}
		}
		return true;
	}
};
} // namespace

void HydrationBubble::update(double p_delta) {
	ERR_FAIL_COND(!Thread::is_main_thread());
	Node3D *parent = ObjectDB::get_instance<Node3D>(host);
	if (!parent || busy || !parent->is_inside_tree()) {
		return;
	}
	busy = true;
	const uint64_t frame_started = OS::get_singleton()->get_ticks_usec();
	OriginShifter *shifter = OriginShifter::get_singleton();
	shifter->sync_from_world_state();
	const PackedFloat64Array origin = shifter->get_origin_64();
	StreamingServer *streaming = StreamingServer::get_singleton();
	LocalVector<BubbleInvoker> influences;
	for (ObjectID id : streaming->get_invokers()) {
		StreamingInvoker *invoker = ObjectDB::get_instance<StreamingInvoker>(id);
		if (!invoker || !invoker->is_inside_tree() || invoker->is_queued_for_deletion() || invoker->get_world_3d() != parent->get_world_3d()) {
			continue;
		}
		invoker->sample_for_hydration(p_delta);
		BubbleInvoker influence;
		influence.position = invoker->get_global_position();
		influence.forward = -invoker->get_global_transform().basis.get_column(2).normalized();
		influence.velocity_axis = invoker->get_velocity().normalized();
		influence.lead = MIN(invoker->get_velocity().length() * invoker->get_lookahead(), 60.0);
		influence.center = influence.position + influence.velocity_axis * influence.lead;
		if (Camera3D *camera = invoker->get_viewport()->get_camera_3d()) {
			influence.frustum = camera->get_frustum();
			influence.forward = -camera->get_global_transform().basis.get_column(2).normalized();
			influence.position = camera->get_global_position();
		}
		influences.push_back(influence);
	}

	SimulationServer *sim = SimulationServer::get_singleton();
	LocalVector<RID> stale;
	for (const KeyValue<RID, ActiveActor> &entry : active) {
		if (!sim->has_agent(entry.key) || !get_active_actor(entry.key)) {
			stale.push_back(entry.key);
		}
	}
	for (RID rid : stale) {
		_release(rid, false);
	}

	LocalVector<Candidate> promote;
	LocalVector<Candidate> demote;
	LocalVector<Candidate> recyclable;
	struct ProxyCandidate {
		RID rid;
		uint32_t index;
	};
	LocalVector<ProxyCandidate> proxies;
	for (uint32_t i = 0; i < sim->get_agent_count(); i++) {
		const RID rid = sim->get_agent_rid_at(i);
		Archetype **archetype = archetypes.getptr(sim->get_agent_archetype_ptr()[i]);
		if (!archetype) {
			continue;
		}
		const Archetype *type = *archetype;
		Node3D *root = get_active_actor(rid);
		const Vector3 position = root ? root->get_global_position() : Vector3(sim->get_agent_pos_x_ptr()[i] - origin[0], sim->get_agent_pos_y_ptr()[i] - origin[1], sim->get_agent_pos_z_ptr()[i] - origin[2]);
		const double radius = root ? type->exit : type->enter;
		double distance_squared = INFINITY;
		bool in_proxy_range = false;
		bool visible = false;
		bool behind = !influences.is_empty();
		for (const BubbleInvoker &influence : influences) {
			const double invoker_distance = influence.distance_squared(position, radius);
			distance_squared = MIN(distance_squared, invoker_distance);
			in_proxy_range |= influence.distance_squared(position, type->proxy) <= type->proxy * type->proxy;
			if (invoker_distance <= radius * radius || sim->get_agent_pin_mask_ptr()[i] != 0) {
				visible |= influence.sees(position);
			}
			if (root) {
				behind &= influence.forward.dot(position - influence.position) < 0;
			}
		}
		const bool pinned = sim->get_agent_pin_mask_ptr()[i] != 0;
		Candidate candidate = { rid, distance_squared, pinned, visible };
		if (root) {
			if (!pinned && distance_squared > radius * radius) {
				demote.push_back(candidate);
			} else if (!pinned && behind && type->overflow_policy == RECYCLE_FARTHEST_UNPINNED) {
				recyclable.push_back(candidate);
			}
		} else if (pinned || distance_squared <= radius * radius) {
			promote.push_back(candidate);
		}
		if (in_proxy_range && type->mesh.is_valid()) {
			proxies.push_back({ rid, i });
		}
		sim->get_agent_hydration_tier_ptr()[i] = root ? SimulationServer::TIER_2_HYDRATED_NODE : SimulationServer::TIER_0_ABSTRACT_SOA;
	}

	promote.sort();
	demote.sort();
	recyclable.sort();
	const uint64_t started = OS::get_singleton()->get_ticks_usec();
	constexpr uint64_t budget_usec = 1500;
	// Include classification in the frame budget. Leave time for an indivisible
	// SceneTree operation and for submitting every visible proxy after transitions.
	const uint64_t reserve = CLAMP(last_proxy_usec * 2 + 250, uint64_t(500), uint64_t(1000));
	const uint64_t deadline = frame_started + budget_usec - reserve;
	int released = 0;
	int claimed = 0;
	for (int i = int(demote.size()) - 1; i >= 0 && released < 6 && OS::get_singleton()->get_ticks_usec() < deadline; i--) {
		const RID rid = demote[i].rid;
		if (active.has(rid) && !sim->is_agent_pinned(rid)) {
			_release(rid, true);
			released++;
		}
	}
	for (const Candidate &candidate : promote) {
		if (claimed == 4 || OS::get_singleton()->get_ticks_usec() >= deadline) {
			break;
		}
		if (!sim->has_agent(candidate.rid)) {
			continue;
		}
		Archetype *type = archetypes[sim->get_agent_archetype(candidate.rid)];
		if (type->pool.get_available() == 0 && released < 6 && type->overflow_policy == RECYCLE_FARTHEST_UNPINNED) {
			for (int i = int(recyclable.size()) - 1; i >= 0; i--) {
				const Candidate &victim = recyclable[i];
				if (!candidate.pinned && !candidate.visible && victim.distance_squared <= candidate.distance_squared) {
					break;
				}
				if (active.has(victim.rid) && active[victim.rid].type == type && !sim->is_agent_pinned(victim.rid)) {
					_release(victim.rid, true);
					released++;
					break;
				}
			}
		}
		if (type->pool.get_available() && OS::get_singleton()->get_ticks_usec() < deadline && sim->has_agent(candidate.rid)) {
			_hydrate(candidate.rid, type, parent);
			claimed++;
		}
	}
	const uint64_t proxy_start = OS::get_singleton()->get_ticks_usec();
	last_transition_usec = proxy_start - started;

	for (const KeyValue<StringName, Archetype *> &entry : archetypes) {
		entry.value->proxies.begin();
	}
	for (const ProxyCandidate &proxy : proxies) {
		// A host signal during a transition can remove an agent and compact SoA.
		const int i = sim->get_agent_rid_at(proxy.index) == proxy.rid ? int(proxy.index) : sim->get_agent_soa_index(proxy.rid);
		if (i < 0 || sim->get_agent_hydration_tier_ptr()[i] == SimulationServer::TIER_2_HYDRATED_NODE) {
			continue;
		}
		const Vector3 position(sim->get_agent_pos_x_ptr()[i] - origin[0], sim->get_agent_pos_y_ptr()[i] - origin[1], sim->get_agent_pos_z_ptr()[i] - origin[2]);
		archetypes[sim->get_agent_archetype_ptr()[i]]->proxies.append(Transform3D(Basis(Vector3(0, 1, 0), sim->get_agent_rot_y_ptr()[i]), position));
		sim->get_agent_hydration_tier_ptr()[i] = SimulationServer::TIER_1_BATCHED_PROXY;
	}
	for (const KeyValue<StringName, Archetype *> &entry : archetypes) {
		entry.value->proxies.submit();
	}
	const uint64_t finished = OS::get_singleton()->get_ticks_usec();
	last_proxy_usec = finished - proxy_start;
	last_update_usec = finished - frame_started;
	busy = false;
}

void HydrationBubble::set_entity_pin(RID p_rid, bool p_pinned, const StringName &p_reason) {
	ERR_FAIL_COND(!Thread::is_main_thread());
	SimulationServer *sim = SimulationServer::get_singleton();
	const uint32_t flag = HydratableActor::get_pin_flag(p_reason);
	ERR_FAIL_COND(!flag || !sim->has_agent(p_rid));
	const uint32_t mask = sim->get_agent_pin_mask(p_rid);
	sim->set_agent_pin_mask(p_rid, p_pinned ? mask | flag : mask & ~flag);
}

Dictionary HydrationBubble::get_stats() const {
	int counts[3] = {};
	SimulationServer *sim = SimulationServer::get_singleton();
	for (uint32_t i = 0; i < sim->get_agent_count(); i++) {
		const uint8_t tier = sim->get_agent_hydration_tier_ptr()[i];
		if (tier < 3) {
			counts[tier]++;
		}
	}
	Dictionary pools;
	for (const KeyValue<StringName, Archetype *> &entry : archetypes) {
		Dictionary pool;
		pool["capacity"] = entry.value->capacity;
		pool["available"] = entry.value->pool.get_available();
		pools[entry.key] = pool;
	}
	Dictionary result;
	result["tier0"] = counts[0];
	result["tier1"] = counts[1];
	result["tier2"] = counts[2];
	result["pools"] = pools;
	result["hydrations"] = hydrations;
	result["dehydrations"] = dehydrations;
	result["last_transition_usec"] = last_transition_usec;
	result["last_update_usec"] = last_update_usec;
	return result;
}

static Dictionary command_result(Error p_error) {
	Dictionary result;
	result["ok"] = p_error == OK;
	result["output"] = error_names[p_error];
	result["error_code"] = p_error;
	return result;
}

Variant HydrationBubble::_cmd_radius(const StringName &p_type, double p_enter, double p_exit) {
	Archetype **type = archetypes.getptr(p_type);
	return command_result(type ? set_radii(p_type, p_enter, p_exit, MAX((*type)->proxy, p_exit)) : ERR_DOES_NOT_EXIST);
}

Variant HydrationBubble::_cmd_pin(int64_t p_id) {
	RID rid = RID::from_uint64(p_id);
	if (!SimulationServer::get_singleton()->has_agent(rid)) {
		return command_result(ERR_DOES_NOT_EXIST);
	}
	set_entity_pin(rid, true, SNAME("debug"));
	return command_result(OK);
}

Variant HydrationBubble::_cmd_unpin(int64_t p_id) {
	RID rid = RID::from_uint64(p_id);
	if (!SimulationServer::get_singleton()->has_agent(rid)) {
		return command_result(ERR_DOES_NOT_EXIST);
	}
	set_entity_pin(rid, false, SNAME("debug"));
	return command_result(OK);
}

HydrationBubble::HydrationBubble() {
	singleton = this;
}

HydrationBubble::~HydrationBubble() {
	stop();

	singleton = nullptr;
}

void HydrationBubble::_bind_methods() {
	ClassDB::bind_method(D_METHOD("configure_archetype", "archetype", "scene", "capacity", "proxy_mesh", "overflow_policy"), &HydrationBubble::configure_archetype, DEFVAL(Ref<Mesh>()), DEFVAL(KEEP_TIER1_PROXY));
	ClassDB::bind_method(D_METHOD("set_radii", "archetype", "enter", "exit", "proxy"), &HydrationBubble::set_radii);
	ClassDB::bind_method(D_METHOD("start", "parent"), &HydrationBubble::start);
	ClassDB::bind_method(D_METHOD("stop"), &HydrationBubble::stop);
	ClassDB::bind_method(D_METHOD("get_stats"), &HydrationBubble::get_stats);
	ClassDB::bind_method(D_METHOD("get_active_actor", "entity_rid"), &HydrationBubble::get_active_actor);
	ClassDB::bind_method(D_METHOD("set_entity_pin", "entity_rid", "pinned", "reason"), &HydrationBubble::set_entity_pin);
	BIND_ENUM_CONSTANT(KEEP_TIER1_PROXY);
	BIND_ENUM_CONSTANT(RECYCLE_FARTHEST_UNPINNED);
}
