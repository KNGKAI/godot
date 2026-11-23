/**************************************************************************/
/*  simulation_server.h                                                   */
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

#include "sim_module.h"

#include "core/math/transform_3d.h"
#include "core/math/vector3.h"
#include "core/object/class_db.h"
#include "core/object/object.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/mutex.h"
#include "core/templates/a_hash_map.h"
#include "core/templates/local_vector.h"
#include "core/templates/rid_owner.h"
#include "core/variant/dictionary.h"

#include <atomic>

class SimulationServer : public Object {
	GDCLASS(SimulationServer, Object);

public:
	enum AgentHydrationTier : uint8_t {
		TIER_0_ABSTRACT_SOA = 0,
		TIER_1_BATCHED_PROXY = 1,
		TIER_2_HYDRATED_NODE = 2,
	};

	enum AgentPinFlags : uint32_t {
		PIN_NONE = 0,
		PIN_MISSION = 1 << 0,
		PIN_SEQUENCE = 1 << 1,
		PIN_PLAYER_OCCUPIED = 1 << 2,
		PIN_COMBAT_LOCK = 1 << 3,
		PIN_DEBUG_COMMAND = 1 << 4,
	};

	struct SimModuleEntry {
		RID self_rid;
		Ref<SimModule> module;
		double accumulator = 0.0;
		uint64_t last_tick_duration_usec = 0;
		uint64_t ticks_executed = 0;
		bool initialized = false;
	};

	struct SimAgentSlot {
		RID self_rid;
		uint32_t soa_index = 0;
	};

	struct RewindSnapshot {
		uint64_t tick_index = 0;
		double sim_time_elapsed = 0.0;
		PackedByteArray binary_data;
	};

private:
	static inline SimulationServer *singleton = nullptr;

	// Thread-safe RID_Owner allocators (§3.2)
	mutable RID_Owner<SimModuleEntry, true> module_owner;
	mutable RID_Owner<SimAgentSlot, true> agent_owner;

	LocalVector<RID> registered_module_rids;

	// Built-in Contiguous Agent SoA Table (§3.9.1 & Phase 2/5 Bridge)
	AHashMap<uint64_t, uint32_t> agent_rid_to_soa;
	LocalVector<RID> agent_rids;
	LocalVector<double> agent_pos_x;
	LocalVector<double> agent_pos_y;
	LocalVector<double> agent_pos_z;
	LocalVector<Vector3> agent_vel;
	LocalVector<float> agent_rot_y;
	LocalVector<StringName> agent_archetype;
	LocalVector<uint32_t> agent_state_flags;
	LocalVector<float> agent_health;
	LocalVector<float> agent_spline_cursor;
	LocalVector<uint8_t> agent_hydration_tier;
	LocalVector<uint32_t> agent_pin_mask;
	LocalVector<PackedByteArray> agent_custom_blob;

	bool kinematically_integrate_tier0_tier1_agents = true;

	// Concurrency Invariants #1, #2, #3 tracking
	std::atomic<uint64_t> total_tasks_dispatched{ 0 };
	std::atomic<uint64_t> total_tasks_awaited{ 0 };
	std::atomic<uint64_t> nested_wait_violations{ 0 };

	struct WorkerModuleTaskContext {
		SimModuleEntry *entry = nullptr;
		double step_delta = 0.0;
	};

	struct WorkerAgentGroupContext {
		SimulationServer *server = nullptr;
		double step_delta = 0.0;
	};

	static void _worker_execute_module_task(void *p_userdata);
	static void _worker_integrate_agents_group(void *p_userdata, uint32_t p_index);

	// Rewind Snapshot Ring Buffer (§4.2)
	bool rewind_capture_enabled = false;
	uint32_t snapshot_interval_ticks = 20;
	uint32_t max_rewind_snapshots = 128;
	uint32_t snapshot_write_cursor = 0;
	LocalVector<RewindSnapshot> rewind_ring;

	void _capture_rewind_snapshot_if_due(uint64_t p_tick_index);

	// Static callbacks connected to SandboxClock & SandboxSerializer
	static void _on_clock_sim_step(double p_sim_delta, uint64_t p_tick_index);
	static void _on_serializer_write_sim_modules(Dictionary &r_modules);
	static Error _on_serializer_read_sim_modules(const Dictionary &p_section);

	Variant _cmd_sim_rewind(int64_t p_ticks);
	Variant _cmd_sim_stats();

protected:
	static void _bind_methods();

public:
	static SimulationServer *get_singleton() { return singleton; }

	// Lifecycle & Hook Registration
	void bind_core_hooks();
	void clear();

	// SimModule Registration & Execution
	RID register_module(const Ref<SimModule> &p_module);
	void unregister_module(RID p_module_rid);
	bool has_module(RID p_module_rid) const { return module_owner.owns(p_module_rid); }
	uint32_t get_module_count() const { return registered_module_rids.size(); }
	Dictionary get_module_metrics(RID p_module_rid) const;
#ifdef DEBUG_ENABLED
	Dictionary get_debug_module_metrics() const;
#endif

	void step(double p_sim_delta, uint64_t p_tick_index = 0);

	// Concurrency Invariant #1 & #2 Diagnostics (Gate 2)
	static bool is_on_sim_worker_thread();
	Error verify_no_nested_wait();
	uint64_t get_total_tasks_dispatched() const { return total_tasks_dispatched.load(std::memory_order_acquire); }
	uint64_t get_total_tasks_awaited() const { return total_tasks_awaited.load(std::memory_order_acquire); }
	int64_t get_leaked_task_count() const { return (int64_t)get_total_tasks_dispatched() - (int64_t)get_total_tasks_awaited(); }
	uint64_t get_nested_wait_violations() const { return nested_wait_violations.load(std::memory_order_acquire); }

	// Built-in Agent SoA Table API (§3.9)
	RID create_agent(const StringName &p_archetype_id, double p_pos_x = 0.0, double p_pos_y = 0.0, double p_pos_z = 0.0, const Vector3 &p_velocity = Vector3(), float p_rot_y = 0.0f, float p_health = 100.0f, uint32_t p_state_flags = 0);
	void free_agent(RID p_agent_rid);
	bool has_agent(RID p_agent_rid) const { return agent_owner.owns(p_agent_rid); }
	uint32_t get_agent_count() const { return agent_rids.size(); }
	int32_t get_agent_soa_index(RID p_agent_rid) const;
	RID get_agent_rid_at(uint32_t p_soa_index) const { return p_soa_index < agent_rids.size() ? agent_rids[p_soa_index] : RID(); }

	void set_agent_position_64(RID p_agent_rid, double p_x, double p_y, double p_z);
	void get_agent_position_64(RID p_agent_rid, double &r_x, double &r_y, double &r_z) const;
	Vector3 get_agent_local_position(RID p_agent_rid) const;
	Transform3D get_agent_local_transform(RID p_agent_rid) const;

	void set_agent_velocity(RID p_agent_rid, const Vector3 &p_vel);
	Vector3 get_agent_velocity(RID p_agent_rid) const;

	void set_agent_rotation_y(RID p_agent_rid, float p_rot_y);
	float get_agent_rotation_y(RID p_agent_rid) const;

	StringName get_agent_archetype(RID p_agent_rid) const;

	void set_agent_state_flags(RID p_agent_rid, uint32_t p_flags);
	uint32_t get_agent_state_flags(RID p_agent_rid) const;

	void set_agent_health(RID p_agent_rid, float p_health);
	float get_agent_health(RID p_agent_rid) const;

	void set_agent_spline_cursor(RID p_agent_rid, float p_cursor);
	float get_agent_spline_cursor(RID p_agent_rid) const;

	void set_agent_hydration_tier(RID p_agent_rid, uint8_t p_tier);
	uint8_t get_agent_hydration_tier(RID p_agent_rid) const;

	void set_agent_pin_mask(RID p_agent_rid, uint32_t p_pin_mask);
	uint32_t get_agent_pin_mask(RID p_agent_rid) const;
	bool is_agent_pinned(RID p_agent_rid) const;

	void set_agent_custom_blob(RID p_agent_rid, const PackedByteArray &p_blob);
	PackedByteArray get_agent_custom_blob(RID p_agent_rid) const;

	// Raw SoA column pointers for HydrationBubble & high-throughput WorkerThreadPool tasks
	const RID *get_agent_rids_ptr() const { return agent_rids.ptr(); }
	double *get_agent_pos_x_ptr() { return agent_pos_x.ptr(); }
	const double *get_agent_pos_x_ptr() const { return agent_pos_x.ptr(); }
	double *get_agent_pos_y_ptr() { return agent_pos_y.ptr(); }
	const double *get_agent_pos_y_ptr() const { return agent_pos_y.ptr(); }
	double *get_agent_pos_z_ptr() { return agent_pos_z.ptr(); }
	const double *get_agent_pos_z_ptr() const { return agent_pos_z.ptr(); }
	Vector3 *get_agent_vel_ptr() { return agent_vel.ptr(); }
	const Vector3 *get_agent_vel_ptr() const { return agent_vel.ptr(); }
	float *get_agent_rot_y_ptr() { return agent_rot_y.ptr(); }
	const float *get_agent_rot_y_ptr() const { return agent_rot_y.ptr(); }
	const StringName *get_agent_archetype_ptr() const { return agent_archetype.ptr(); }
	uint32_t *get_agent_state_flags_ptr() { return agent_state_flags.ptr(); }
	float *get_agent_health_ptr() { return agent_health.ptr(); }
	uint8_t *get_agent_hydration_tier_ptr() { return agent_hydration_tier.ptr(); }
	const uint8_t *get_agent_hydration_tier_ptr() const { return agent_hydration_tier.ptr(); }
	uint32_t *get_agent_pin_mask_ptr() { return agent_pin_mask.ptr(); }
	const uint32_t *get_agent_pin_mask_ptr() const { return agent_pin_mask.ptr(); }

	PackedByteArray serialize_agent_soa_blob() const;
	Error deserialize_agent_soa_blob(const PackedByteArray &p_blob);

	// Deterministic Rewind API (§4.2)
	void set_rewind_capture_enabled(bool p_enabled) { rewind_capture_enabled = p_enabled; }
	bool is_rewind_capture_enabled() const { return rewind_capture_enabled; }

	void set_snapshot_interval_ticks(uint32_t p_interval);
	uint32_t get_snapshot_interval_ticks() const { return snapshot_interval_ticks; }

	void capture_rewind_snapshot_now();
	uint32_t get_rewind_snapshot_count() const { return rewind_ring.size(); }
	void clear_rewind_snapshots();
	Error rewind(uint64_t p_ticks_back);

	SimulationServer();
	~SimulationServer();
};
