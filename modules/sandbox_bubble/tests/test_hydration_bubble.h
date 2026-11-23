/**************************************************************************/
/*  test_hydration_bubble.h                                              */
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

#include "../hydratable_actor.h"
#include "../hydration_bubble.h"

#include "core/templates/hash_set.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "tests/test_macros.h"

#include "modules/sandbox_core/command_bus.h"
#include "modules/sandbox_core/sandbox_serializer.h"
#include "modules/sandbox_core/world_state.h"
#include "modules/sandbox_sim/simulation_server.h"
#include "modules/sandbox_streaming/origin_shifter.h"
#include "modules/sandbox_streaming/streaming_invoker.h"

namespace TestHydrationBubble {

struct District {
	HydrationBubble *bubble = HydrationBubble::get_singleton();
	SimulationServer *sim = SimulationServer::get_singleton();
	WorldState *state = WorldState::get_singleton();
	OriginShifter *shifter = OriginShifter::get_singleton();
	Array previous_origin = state->get_world_origin_64_array();
	Node3D *world = memnew(Node3D);
	StreamingInvoker *invoker = memnew(StreamingInvoker);
	StringName archetype = SNAME("gate7_pedestrian");

	District() {
		bubble->stop();
		sim->clear();
		state->set_world_origin_64(0, 0, 0);
		shifter->sync_from_world_state();
		invoker->set_lookahead(0.0);
		world->add_child(invoker);
		SceneTree::get_singleton()->get_root()->add_child(world);
	}

	~District() {
		bubble->stop();
		sim->clear();
		memdelete(world);
		state->set_world_origin_64_array(previous_origin);
		shifter->sync_from_world_state();
	}

	void start(int p_capacity, double p_enter = 10, double p_exit = 15, double p_proxy = 30, int p_policy = HydrationBubble::KEEP_TIER1_PROXY, bool p_with_proxy = true) {
		Node3D *prefab = memnew(Node3D);
		prefab->add_to_group(SNAME("gate7_pool_shells"), true);
		HydratableActor *component = memnew(HydratableActor);
		component->set_name("Hydratable");
		component->set_archetype_id(archetype);
		prefab->add_child(component);
		component->set_owner(prefab);
		Ref<PackedScene> scene;
		scene.instantiate();
		const Error err = scene->pack(prefab);
		memdelete(prefab);
		REQUIRE(err == OK);
		Ref<BoxMesh> mesh;
		if (p_with_proxy) {
			mesh.instantiate();
		}
		REQUIRE(bubble->configure_archetype(archetype, scene, p_capacity, mesh, p_policy) == OK);
		REQUIRE(bubble->set_radii(archetype, p_enter, p_exit, p_proxy) == OK);
		REQUIRE(bubble->start(world) == OK);
	}

	void update() {
		bubble->update(1.0 / 60.0);
	}

	void await_residency(RID p_rid, bool p_hydrated) {
		// A busy frame may exhaust the classification budget before starting a transition.
		for (int frame = 0; frame < 60 && (bubble->get_active_actor(p_rid) != nullptr) != p_hydrated; frame++) {
			update();
		}
	}
};

static void collect_pool_shell(Object *p_object, void *p_ids) {
	Node *node = Object::cast_to<Node>(p_object);
	if (node && node->is_in_group(SNAME("gate7_pool_shells"))) {
		static_cast<HashSet<ObjectID> *>(p_ids)->insert(node->get_instance_id());
	}
}

TEST_CASE("[SceneTree][SandboxBubble] Opt-in commands and a stationary shell preserve simulation state") {
	District district;
	CommandBus *commands = CommandBus::get_singleton();
	CHECK_FALSE(commands->has_command(SNAME("bubble.stats")));
	district.start(1, 10, 15, 30, HydrationBubble::KEEP_TIER1_PROXY, false);
	const double original_x = 2.000000000123;
	const RID rid = district.sim->create_agent(district.archetype, original_x);
	const RID distant = district.sim->create_agent(district.archetype, 25);
	district.update();
	CHECK(district.sim->get_agent_hydration_tier(distant) == SimulationServer::TIER_0_ABSTRACT_SOA);
	REQUIRE(district.bubble->get_active_actor(rid) != nullptr);
	CHECK(bool(commands->execute("bubble.stats")["ok"]));
	const String pin = vformat("bubble.pin %d", rid.get_id());
	CHECK_FALSE(bool(commands->execute(pin, CommandBus::PERMISSION_USER)["ok"]));
	CHECK(bool(commands->execute(pin, CommandBus::PERMISSION_ADMIN)["ok"]));
	CHECK(district.sim->get_agent_pin_mask(rid) == SimulationServer::PIN_DEBUG_COMMAND);
	CHECK_FALSE(bool(commands->execute("bubble.radius gate7_pedestrian 30 20", CommandBus::PERMISSION_ADMIN)["ok"]));
	CHECK(bool(commands->execute("bubble.radius gate7_pedestrian 16 20", CommandBus::PERMISSION_ADMIN)["ok"]));
	CHECK(bool(commands->execute(vformat("bubble.unpin %d", rid.get_id()), CommandBus::PERMISSION_ADMIN)["ok"]));
	district.bubble->flush_active_actors_to_soa();
	double x, y, z;
	district.sim->get_agent_position_64(rid, x, y, z);
	CHECK(x == original_x);
	district.bubble->stop();
	CHECK_FALSE(commands->has_command(SNAME("bubble.stats")));
	CHECK(district.sim->get_agent_hydration_tier(rid) == SimulationServer::TIER_0_ABSTRACT_SOA);
}

TEST_CASE("[SceneTree][SandboxBubble][Gate7] Traverse 2,000 moving SoA agents with a fixed pool") {
	District district;
	constexpr int BLOCKS = 10;
	constexpr int AGENTS_PER_BLOCK = 200;
	constexpr int POOL_CAPACITY = 8;
	district.start(POOL_CAPACITY, 20, 30, 48);

	HashSet<ObjectID> warm_shells;
	ObjectDB::debug_objects(collect_pool_shell, &warm_shells);
	REQUIRE(warm_shells.size() == POOL_CAPACITY);
	for (int block = 0; block < BLOCKS; block++) {
		for (int i = 0; i < AGENTS_PER_BLOCK; i++) {
			district.sim->create_agent(district.archetype, block * 80 + (i % 10), 0, (i / 10) * 0.5, Vector3(0, 0, 0.03125));
		}
	}
	REQUIRE(district.sim->get_agent_count() == 2000);
	const uint64_t tasks_before = district.sim->get_total_tasks_dispatched();
	uint64_t ticks = 0;
	int64_t hydrations = 0;
	int64_t dehydrations = 0;
	for (int block = 0; block < BLOCKS; block++) {
		district.invoker->set_position(Vector3(block * 80, 0, 0));
		for (int frame = 0; frame < 24; frame++) {
			district.sim->step(0.25, ++ticks);
			district.update();
			const Dictionary stats = district.bubble->get_stats();
			const int64_t next_hydrations = stats["hydrations"];
			const int64_t next_dehydrations = stats["dehydrations"];
			CHECK(next_hydrations - hydrations <= 4);
			CHECK(next_dehydrations - dehydrations <= 6);
			hydrations = next_hydrations;
			dehydrations = next_dehydrations;
			REQUIRE(int(stats["tier2"]) <= POOL_CAPACITY);
		}

		const Dictionary stats = district.bubble->get_stats();
		CHECK(int(stats["tier2"]) == POOL_CAPACITY);
		CHECK(int(stats["tier1"]) == AGENTS_PER_BLOCK - POOL_CAPACITY);
		CHECK(int(stats["tier0"]) == (BLOCKS - 1) * AGENTS_PER_BLOCK);
		HashSet<ObjectID> active_in_block;
		for (int i = 0; i < AGENTS_PER_BLOCK; i++) {
			const RID rid = district.sim->get_agent_rid_at(block * AGENTS_PER_BLOCK + i);
			Node3D *actor = district.bubble->get_active_actor(rid);
			if (actor) {
				CHECK(warm_shells.has(actor->get_instance_id()));
				CHECK_FALSE(actor->is_queued_for_deletion());
				CHECK(district.sim->get_agent_hydration_tier(rid) == SimulationServer::TIER_2_HYDRATED_NODE);
				active_in_block.insert(actor->get_instance_id());
			} else {
				CHECK(district.sim->get_agent_hydration_tier(rid) == SimulationServer::TIER_1_BATCHED_PROXY);
			}
		}
		CHECK(active_in_block.size() == POOL_CAPACITY);
	}
	CHECK(hydrations >= BLOCKS * POOL_CAPACITY);
	CHECK(dehydrations >= (BLOCKS - 1) * POOL_CAPACITY);
	CHECK(district.sim->get_total_tasks_dispatched() - tasks_before >= ticks);
	CHECK(district.sim->get_leaked_task_count() == 0);
	// This agent spent the traversal outside the eight closest candidates.
	const RID last = district.sim->get_agent_rid_at(1999);
	CHECK(district.sim->get_agent_local_position(last).z > 9.5);
	HashSet<ObjectID> final_shells;
	ObjectDB::debug_objects(collect_pool_shell, &final_shells);
	CHECK(final_shells.size() == warm_shells.size());
	for (ObjectID id : warm_shells) {
		CHECK(final_shells.has(id));
	}
}

TEST_CASE("[SceneTree][SandboxBubble][Gate7] Enter and exit boundaries keep their tier throughout the hysteresis band") {
	District district;
	district.start(1);
	const RID rid = district.sim->create_agent(district.archetype, 12);
	district.update();
	REQUIRE(district.sim->get_agent_hydration_tier(rid) == SimulationServer::TIER_1_BATCHED_PROXY);

	for (int i = 0; i < 60; i++) {
		district.sim->set_agent_position_64(rid, i % 2 ? 10.125 : 14.875, 0, 0);
		district.update();
		CHECK(district.bubble->get_active_actor(rid) == nullptr);
	}
	district.sim->set_agent_position_64(rid, 10, 0, 0);
	district.update();
	Node3D *actor = district.bubble->get_active_actor(rid);
	REQUIRE(actor != nullptr);
	for (int i = 0; i < 60; i++) {
		actor->set_global_position(Vector3(i % 2 ? 10.125 : 15.0, 0, 0));
		district.update();
		REQUIRE(district.bubble->get_active_actor(rid) == actor);
	}
	actor->set_global_position(Vector3(15.125, 0, 0));
	district.update();
	CHECK(district.bubble->get_active_actor(rid) == nullptr);
	CHECK(district.sim->get_agent_hydration_tier(rid) == SimulationServer::TIER_1_BATCHED_PROXY);
	CHECK(int(district.bubble->get_stats()["hydrations"]) == 1);
	CHECK(int(district.bubble->get_stats()["dehydrations"]) == 1);
	district.sim->set_agent_position_64(rid, 30, 0, 0);
	district.update();
	CHECK(district.sim->get_agent_hydration_tier(rid) == SimulationServer::TIER_1_BATCHED_PROXY);
	district.sim->set_agent_position_64(rid, 30.125, 0, 0);
	district.update();
	CHECK(district.sim->get_agent_hydration_tier(rid) == SimulationServer::TIER_0_ABSTRACT_SOA);
}

TEST_CASE("[SceneTree][SandboxBubble] A moving invoker hydrates ahead and a second invoker keeps a distant actor live") {
	District district;
	district.start(2);
	district.invoker->set_lookahead(1);
	StreamingInvoker *second = memnew(StreamingInvoker);
	second->set_position(Vector3(-1000, 0, 0));
	district.world->add_child(second);
	const RID distant = district.sim->create_agent(district.archetype, -1000);
	const RID ahead = district.sim->create_agent(district.archetype, 65);
	const RID behind = district.sim->create_agent(district.archetype, -15);
	district.bubble->update(0.25);
	REQUIRE(district.bubble->get_active_actor(distant) != nullptr);
	CHECK(district.bubble->get_active_actor(ahead) == nullptr);
	district.invoker->set_position(Vector3(25, 0, 0));
	district.bubble->update(0.25);
	CHECK(district.invoker->get_velocity().x > 0);
	CHECK(district.bubble->get_active_actor(ahead) != nullptr);
	CHECK(district.bubble->get_active_actor(behind) == nullptr);
	REQUIRE(district.bubble->get_active_actor(distant) != nullptr);
	memdelete(second);
	district.bubble->update(0.25);
	CHECK(district.bubble->get_active_actor(distant) == nullptr);
}

TEST_CASE("[SceneTree][SandboxBubble] A full pool recycles the farthest unpinned actor behind the invoker") {
	District district;
	district.start(2, 10, 15, 30, HydrationBubble::RECYCLE_FARTHEST_UNPINNED);
	// The default forward direction is -Z. Both initial actors are behind the invoker.
	const RID far = district.sim->create_agent(district.archetype, 0, 0, 9);
	const RID near = district.sim->create_agent(district.archetype, 0, 0, 5);
	district.update();
	district.update();
	Node3D *far_shell = district.bubble->get_active_actor(far);
	Node3D *near_shell = district.bubble->get_active_actor(near);
	REQUIRE(far_shell != nullptr);
	REQUIRE(near_shell != nullptr);
	const RID front = district.sim->create_agent(district.archetype, 0, 0, -2);
	SUBCASE("Both shells are eligible") {
		district.update();
		CHECK(district.bubble->get_active_actor(front) == far_shell);
		CHECK(district.bubble->get_active_actor(near) == near_shell);
		CHECK(district.sim->get_agent_hydration_tier(far) == SimulationServer::TIER_1_BATCHED_PROXY);
	}
	SUBCASE("The farthest actor has an active pin") {
		district.bubble->set_entity_pin(far, true, SNAME("sequence"));
		district.update();
		CHECK(district.bubble->get_active_actor(front) == near_shell);
		CHECK(district.bubble->get_active_actor(far) == far_shell);
		CHECK(district.sim->get_agent_hydration_tier(near) == SimulationServer::TIER_1_BATCHED_PROXY);
	}
}

TEST_CASE("[SceneTree][SandboxBubble][Gate7] Five hundred round trips preserve actor state and independent pin owners") {
	District district;
	district.start(1);
	const RID rid = district.sim->create_agent(district.archetype);
	const StringName reasons[] = { SNAME("mission"), SNAME("sequence"), SNAME("player_occupied"), SNAME("combat_lock"), SNAME("debug") };
	const uint32_t flags[] = { SimulationServer::PIN_MISSION, SimulationServer::PIN_SEQUENCE, SimulationServer::PIN_PLAYER_OCCUPIED, SimulationServer::PIN_COMBAT_LOCK, SimulationServer::PIN_DEBUG_COMMAND };
	for (int cycle = 0; cycle < 500; cycle++) {
		const Vector3 velocity(cycle * 0.5, -2, 3);
		const float health = 100.0f - cycle * 0.125f;
		const uint32_t damage = 0x10000 + cycle;
		const PackedByteArray blob = { 0xff, uint8_t(cycle >> 8), uint8_t(cycle & 0xff), 0 };
		const Vector3 outside_position(50, cycle * 0.125, 7.5);
		district.sim->set_agent_position_64(rid, 2, 0, 0);
		district.await_residency(rid, true);
		Node3D *actor = district.bubble->get_active_actor(rid);
		REQUIRE(actor != nullptr);
		HydratableActor *component = Object::cast_to<HydratableActor>(actor->get_node_or_null(NodePath("Hydratable")));
		REQUIRE(component != nullptr);
		REQUIRE(component->get_bound_rid() == rid);
		Dictionary state = component->get_state();
		if (cycle > 0) {
			CHECK(Vector3(state["velocity"]) == Vector3((cycle - 1) * 0.5, -2, 3));
			CHECK(float(state["health"]) == 100.0f - (cycle - 1) * 0.125f);
			CHECK(uint32_t(state["state_flags"]) == damage - 1);
			const PackedByteArray previous_blob = { 0xff, uint8_t((cycle - 1) >> 8), uint8_t((cycle - 1) & 0xff), 0 };
			CHECK(PackedByteArray(state["custom_blob"]) == previous_blob);
		}
		state["velocity"] = velocity;
		state["health"] = health;
		state["state_flags"] = damage;
		state["spline_cursor"] = cycle * 0.25;
		state["custom_blob"] = blob;
		component->set_state(state);
		actor->set_global_position(outside_position);
		actor->set_rotation(Vector3(0, 0.5, 0));
		const int first = cycle % 5;
		const int second = (first + 1) % 5;
		component->set_pinned(true, reasons[first]);
		district.bubble->set_entity_pin(rid, true, reasons[second]);
		district.update();
		REQUIRE(district.bubble->get_active_actor(rid) == actor);
		CHECK(district.sim->get_agent_pin_mask(rid) == (flags[first] | flags[second]));
		component->set_pinned(false, reasons[first]);
		district.update();
		REQUIRE(district.bubble->get_active_actor(rid) == actor);
		CHECK(district.sim->get_agent_pin_mask(rid) == flags[second]);
		district.bubble->set_entity_pin(rid, false, reasons[second]);
		district.await_residency(rid, false);
		REQUIRE(district.bubble->get_active_actor(rid) == nullptr);
		CHECK(district.sim->get_agent_pin_mask(rid) == 0);
		CHECK(district.sim->get_agent_local_position(rid) == outside_position);
		CHECK(Math::is_equal_approx(district.sim->get_agent_rotation_y(rid), 0.5f));
		CHECK(district.sim->get_agent_velocity(rid) == velocity);
		CHECK(district.sim->get_agent_health(rid) == health);
		CHECK(district.sim->get_agent_state_flags(rid) == damage);
		CHECK(district.sim->get_agent_spline_cursor(rid) == cycle * 0.25f);
		CHECK(district.sim->get_agent_custom_blob(rid) == blob);
	}
	CHECK(int(district.bubble->get_stats()["hydrations"]) == 500);
	CHECK(int(district.bubble->get_stats()["dehydrations"]) == 500);
}

TEST_CASE("[SceneTree][SandboxBubble][Gate7] Saving flushes live state and loading resynchronizes the active shell") {
	District district;
	district.start(1);
	SandboxSerializer *serializer = SandboxSerializer::get_singleton();
	const RID rid = district.sim->create_agent(district.archetype, 2.5, 0, 0);
	const RID distant = district.sim->create_agent(district.archetype, 1000, 0, 0);
	district.update();
	Node3D *actor = district.bubble->get_active_actor(rid);
	REQUIRE(actor != nullptr);
	HydratableActor *component = Object::cast_to<HydratableActor>(actor->get_node_or_null(NodePath("Hydratable")));
	REQUIRE(component != nullptr);
	const PackedByteArray blob = { 0x00, 0xff, 0x01, 0x00 };
	const Vector3 velocity(4.25, -1, 8);
	const Vector3 position(3.75, 2, 1);
	Dictionary live = component->get_state();
	live["velocity"] = velocity;
	live["health"] = 31.5;
	live["state_flags"] = 0xb04;
	live["spline_cursor"] = 5.25;
	live["custom_blob"] = blob;
	component->set_state(live);
	component->set_pinned(true, SNAME("player_occupied"));
	actor->set_global_position(position);
	actor->set_rotation(Vector3(0, 0.25, 0));
	REQUIRE(district.sim->get_agent_health(rid) == 100.0f);
	REQUIRE(district.sim->get_agent_local_position(rid) != position);
	const uint32_t flushes = serializer->get_pre_save_flush_count();
	PackedByteArray binary;
	String json;
	SUBCASE("Binary envelope") {
		binary = serializer->save_to_binary();
		REQUIRE_FALSE(binary.is_empty());
	}
	SUBCASE("JSON envelope") {
		json = serializer->save_to_json();
		REQUIRE_FALSE(json.is_empty());
	}
	CHECK(serializer->get_pre_save_flush_count() == flushes + 1);
	REQUIRE(district.bubble->get_active_actor(rid) == actor);
	CHECK(component->get_bound_rid() == rid);
	CHECK(district.sim->get_agent_local_position(rid) == position);
	CHECK(district.sim->get_agent_health(rid) == 31.5f);

	Dictionary changed = live.duplicate(true);
	changed["health"] = 0;
	changed["state_flags"] = 0;
	changed["velocity"] = Vector3();
	changed["custom_blob"] = PackedByteArray();
	component->set_state(changed);
	component->set_pinned(false, SNAME("player_occupied"));
	actor->set_global_position(Vector3(900, 0, 0));
	actor->set_rotation(Vector3());
	district.sim->set_agent_health(rid, -1.0f);
	district.sim->set_agent_position_64(distant, -1000, 0, 0);
	REQUIRE((binary.is_empty() ? serializer->load_from_json(json) : serializer->load_from_binary(binary)) == OK);
	actor = district.bubble->get_active_actor(rid);
	REQUIRE(actor != nullptr);
	component = Object::cast_to<HydratableActor>(actor->get_node_or_null(NodePath("Hydratable")));
	REQUIRE(component != nullptr);
	CHECK(component->get_bound_rid() == rid);
	CHECK(actor->get_global_position() == position);
	CHECK(Math::is_equal_approx(actor->get_rotation().y, real_t(0.25)));
	const Dictionary restored = component->get_state();
	CHECK(float(restored["health"]) == 31.5f);
	CHECK(uint32_t(restored["state_flags"]) == 0xb04);
	CHECK(float(restored["spline_cursor"]) == 5.25f);
	CHECK(Vector3(restored["velocity"]) == velocity);
	CHECK(PackedByteArray(restored["custom_blob"]) == blob);
	CHECK(district.sim->get_agent_pin_mask(rid) == SimulationServer::PIN_PLAYER_OCCUPIED);
	CHECK(district.sim->get_agent_hydration_tier(rid) == SimulationServer::TIER_2_HYDRATED_NODE);
	CHECK(district.sim->get_agent_local_position(distant) == Vector3(1000, 0, 0));
	CHECK(district.bubble->get_active_actor(distant) == nullptr);
}

TEST_CASE("[SceneTree][SandboxBubble][Gate7] A floating-origin shift preserves absolute positions across all three tiers") {
	District district;
	district.start(1);
	const real_t distance = MAX(district.shifter->get_shift_threshold() * 2, real_t(4096));
	district.invoker->set_position(Vector3(distance, 0, 0));
	const RID live = district.sim->create_agent(district.archetype, distance + 1.25);
	const RID proxy = district.sim->create_agent(district.archetype, distance + 24.5);
	const RID remote = district.sim->create_agent(district.archetype, distance + 1024.75);
	district.update();
	Node3D *actor = district.bubble->get_active_actor(live);
	REQUIRE(actor != nullptr);
	REQUIRE(district.sim->get_agent_hydration_tier(proxy) == SimulationServer::TIER_1_BATCHED_PROXY);
	REQUIRE(district.sim->get_agent_hydration_tier(remote) == SimulationServer::TIER_0_ABSTRACT_SOA);
	const bool shifted = district.shifter->check_and_shift(district.invoker);
#ifdef REAL_T_IS_DOUBLE
	CHECK_FALSE(shifted);
#else
	REQUIRE(shifted);
#endif
	const double origin_x = district.shifter->get_origin_64()[0];
	CHECK(origin_x + actor->get_global_position().x == distance + 1.25);
	CHECK(origin_x + district.invoker->get_global_position().x == distance);
	district.update();
	REQUIRE(district.bubble->get_active_actor(live) == actor);
	CHECK(district.sim->get_agent_hydration_tier(proxy) == SimulationServer::TIER_1_BATCHED_PROXY);
	CHECK(district.sim->get_agent_hydration_tier(remote) == SimulationServer::TIER_0_ABSTRACT_SOA);
	actor->set_global_position(actor->get_global_position() + Vector3(100, 0, 0));
	district.update();
	REQUIRE(district.bubble->get_active_actor(live) == nullptr);
	double x, y, z;
	district.sim->get_agent_position_64(live, x, y, z);
	CHECK(x == distance + 101.25);
	district.sim->get_agent_position_64(proxy, x, y, z);
	CHECK(x == distance + 24.5);
	district.sim->get_agent_position_64(remote, x, y, z);
	CHECK(x == distance + 1024.75);
}

} // namespace TestHydrationBubble
