/**************************************************************************/
/*  simulation_server.cpp                                                 */
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

#include "simulation_server.h"

#include "modules/sandbox_core/command_bus.h"
#include "modules/sandbox_core/event_bus.h"
#include "modules/sandbox_core/sandbox_clock.h"
#include "modules/sandbox_core/sandbox_schema_registry.h"
#include "modules/sandbox_core/sandbox_serializer.h"
#include "modules/sandbox_core/world_state.h"

#include "core/io/stream_peer.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/os/thread.h"

static thread_local bool tl_in_sim_worker_task = false;

bool SimulationServer::is_on_sim_worker_thread() {
	return tl_in_sim_worker_task;
}

Error SimulationServer::verify_no_nested_wait() {
	if (tl_in_sim_worker_task) {
		nested_wait_violations.fetch_add(1, std::memory_order_relaxed);
		return ERR_BUSY;
	}
	return OK;
}

void SimulationServer::_worker_execute_module_task(void *p_userdata) {
	WorkerModuleTaskContext *ctx = static_cast<WorkerModuleTaskContext *>(p_userdata);
	if (!ctx || !ctx->entry || ctx->entry->module.is_null()) {
		return;
	}
	tl_in_sim_worker_task = true;
	uint64_t t0 = OS::get_singleton() ? OS::get_singleton()->get_ticks_usec() : 0;
	ctx->entry->module->on_tick(ctx->step_delta, WorldState::get_singleton(), EventBus::get_singleton());
	ctx->entry->ticks_executed++;
	if (OS::get_singleton()) {
		ctx->entry->last_tick_duration_usec = OS::get_singleton()->get_ticks_usec() - t0;
	}
	tl_in_sim_worker_task = false;
}

void SimulationServer::_worker_integrate_agents_group(void *p_userdata, uint32_t p_index) {
	WorkerAgentGroupContext *ctx = static_cast<WorkerAgentGroupContext *>(p_userdata);
	SimulationServer *srv = ctx->server;
	if (p_index >= srv->agent_rids.size()) {
		return;
	}
	// Tier 0 and Tier 1 agents are integrated directly in SoA memory; Tier 2 hydrated actors are driven by SceneTree physics.
	if (srv->agent_hydration_tier[p_index] != TIER_2_HYDRATED_NODE) {
		const Vector3 &v = srv->agent_vel[p_index];
		srv->agent_pos_x[p_index] += (double)v.x * ctx->step_delta;
		srv->agent_pos_y[p_index] += (double)v.y * ctx->step_delta;
		srv->agent_pos_z[p_index] += (double)v.z * ctx->step_delta;
	}
}

RID SimulationServer::register_module(const Ref<SimModule> &p_module) {
	ERR_FAIL_COND_V(p_module.is_null(), RID());
	ERR_FAIL_COND_V_MSG(WorldState::get_singleton() && WorldState::get_singleton()->is_structure_locked(), RID(), "Cannot register SimModule during parallel WorkerThreadPool execution.");

	SimModuleEntry entry;
	entry.module = p_module;
	entry.accumulator = 0.0;
	entry.initialized = false;

	RID rid = module_owner.make_rid(entry);
	SimModuleEntry *stored = module_owner.get_or_null(rid);
	if (stored) {
		stored->self_rid = rid;
		stored->module->on_init(WorldState::get_singleton(), EventBus::get_singleton());
		stored->initialized = true;
	}
	registered_module_rids.push_back(rid);

	// Merge declared schema into SandboxSchemaRegistry (§3.7.3).
	Dictionary schema = p_module->get_declared_schema();
	if (!schema.is_empty()) {
		SandboxSchemaRegistry::get_active_registry()->merge_module_schema(schema);
	}

	return rid;
}

void SimulationServer::unregister_module(RID p_module_rid) {
	ERR_FAIL_COND(!module_owner.owns(p_module_rid));
	for (uint32_t i = 0; i < registered_module_rids.size(); i++) {
		if (registered_module_rids[i] == p_module_rid) {
			registered_module_rids.remove_at(i);
			break;
		}
	}
	module_owner.free(p_module_rid);
}

#ifdef DEBUG_ENABLED
Dictionary SimulationServer::get_debug_module_metrics() const {
	Dictionary metrics;
	for (RID rid : registered_module_rids) {
		Dictionary entry = get_module_metrics(rid);
		metrics[entry["module_id"]] = entry;
	}
	return metrics;
}
#endif

Dictionary SimulationServer::get_module_metrics(RID p_module_rid) const {
	Dictionary d;
	SimModuleEntry *entry = module_owner.get_or_null(p_module_rid);
	if (!entry || entry->module.is_null()) {
		return d;
	}
	d["module_id"] = entry->module->get_module_id();
	d["priority"] = entry->module->get_priority();
	d["tick_rate_hz"] = entry->module->get_tick_rate_hz();
	d["thread_safe_tick"] = entry->module->is_thread_safe_tick();
	d["last_tick_duration_usec"] = entry->last_tick_duration_usec;
	d["ticks_executed"] = entry->ticks_executed;
	return d;
}

void SimulationServer::step(double p_sim_delta, uint64_t p_tick_index) {
	// Concurrency Invariant #2: Worker threads must NEVER invoke nested waits or nested simulation steps.
	ERR_FAIL_COND_MSG(verify_no_nested_wait() != OK, "SimulationServer::step cannot be called from inside a worker thread task (Invariant #2: Zero Nested Waits).");
	ERR_FAIL_COND_MSG(!Thread::is_main_thread(), "SimulationServer::step must be called on the main thread.");
	if (p_sim_delta <= 0.0) {
		return;
	}

	// Collect all due modules and sort by priority ascending.
	struct DueModule {
		SimModuleEntry *entry = nullptr;
		double module_delta = 0.0;
		int priority = 0;
	};
	LocalVector<DueModule> due_modules;
	due_modules.reserve(registered_module_rids.size());

	for (uint32_t i = 0; i < registered_module_rids.size(); i++) {
		SimModuleEntry *entry = module_owner.get_or_null(registered_module_rids[i]);
		if (!entry || entry->module.is_null()) {
			continue;
		}
		entry->accumulator += p_sim_delta;
		double period = 1.0 / MAX(0.001, entry->module->get_tick_rate_hz());
		if (entry->accumulator + 1e-9 >= period) {
			double exec_delta = entry->accumulator;
			entry->accumulator = 0.0;
			due_modules.push_back({ entry, exec_delta, entry->module->get_priority() });
		}
	}

	// Insertion sort by priority (stable, zero heap churn for typical module counts).
	for (uint32_t i = 1; i < due_modules.size(); i++) {
		DueModule key = due_modules[i];
		int j = (int)i - 1;
		while (j >= 0 && due_modules[j].priority > key.priority) {
			due_modules[j + 1] = due_modules[j];
			j--;
		}
		due_modules[j + 1] = key;
	}

	// Concurrency Invariant #3: Lock SoA structure sizing before dispatching parallel tasks.
	if (WorldState::get_singleton()) {
		WorldState::get_singleton()->lock_structure();
	}

	WorkerThreadPool *pool = WorkerThreadPool::get_singleton();

	// Integrate Tier 0/1 Agent SoA kinematics across WorkerThreadPool if agents exist.
	if (kinematically_integrate_tier0_tier1_agents && !agent_rids.is_empty()) {
		WorkerAgentGroupContext group_ctx{ this, p_sim_delta };
		if (pool && agent_rids.size() >= 64) {
			WorkerThreadPool::GroupID gid = pool->add_native_group_task(
					&SimulationServer::_worker_integrate_agents_group,
					&group_ctx,
					(int)agent_rids.size(),
					-1,
					true,
					"SimulationServer::IntegrateAgentsSoA");
			total_tasks_dispatched.fetch_add(1, std::memory_order_relaxed);
			pool->wait_for_group_task_completion(gid);
			total_tasks_awaited.fetch_add(1, std::memory_order_relaxed);
		} else {
			for (uint32_t i = 0; i < agent_rids.size(); i++) {
				_worker_integrate_agents_group(&group_ctx, i);
			}
		}
	}

	// Dispatch due SimModules bucketed by priority.
	uint32_t idx = 0;
	while (idx < due_modules.size()) {
		int bucket_priority = due_modules[idx].priority;
		uint32_t bucket_end = idx + 1;
		while (bucket_end < due_modules.size() && due_modules[bucket_end].priority == bucket_priority) {
			bucket_end++;
		}

		LocalVector<WorkerModuleTaskContext> contexts;
		LocalVector<WorkerThreadPool::TaskID> task_ids;
		contexts.resize(bucket_end - idx);

		for (uint32_t k = idx; k < bucket_end; k++) {
			uint32_t local_idx = k - idx;
			contexts[local_idx].entry = due_modules[k].entry;
			contexts[local_idx].step_delta = due_modules[k].module_delta;

			if (pool && due_modules[k].entry->module->is_thread_safe_tick()) {
				WorkerThreadPool::TaskID tid = pool->add_native_task(
						&SimulationServer::_worker_execute_module_task,
						&contexts[local_idx],
						true,
						String(due_modules[k].entry->module->get_module_id()));
				task_ids.push_back(tid);
				total_tasks_dispatched.fetch_add(1, std::memory_order_relaxed);
			} else {
				// Execute main-thread-bound SimModule synchronously.
				uint64_t t0 = OS::get_singleton() ? OS::get_singleton()->get_ticks_usec() : 0;
				due_modules[k].entry->module->on_tick(due_modules[k].module_delta, WorldState::get_singleton(), EventBus::get_singleton());
				due_modules[k].entry->ticks_executed++;
				if (OS::get_singleton()) {
					due_modules[k].entry->last_tick_duration_usec = OS::get_singleton()->get_ticks_usec() - t0;
				}
			}
		}

		// Concurrency Invariant #1: Strictly await every TaskID in this priority bucket before proceeding!
		for (uint32_t t = 0; t < task_ids.size(); t++) {
			const Error err = pool->wait_for_task_completion(task_ids[t]);
			ERR_CONTINUE_MSG(err != OK, "Failed to await a simulation worker task.");
			total_tasks_awaited.fetch_add(1, std::memory_order_relaxed);
		}

		idx = bucket_end;
	}

	// Concurrency Invariant #3: Unlock structure and commit deferred structural additions single-threaded.
	if (WorldState::get_singleton()) {
		WorldState::get_singleton()->unlock_and_commit_structural_changes();
	}

	// Concurrency Invariant #4: Flush deferred EventBus signals on the main thread.
	if (Thread::is_main_thread() && EventBus::get_singleton()) {
		EventBus::get_singleton()->flush_deferred();
	}

	_capture_rewind_snapshot_if_due(p_tick_index);
}

RID SimulationServer::create_agent(const StringName &p_archetype_id, double p_pos_x, double p_pos_y, double p_pos_z, const Vector3 &p_velocity, float p_rot_y, float p_health, uint32_t p_state_flags) {
	ERR_FAIL_COND_V_MSG(WorldState::get_singleton() && WorldState::get_singleton()->is_structure_locked(), RID(), "Cannot resize Agent SoA table during parallel WorkerThreadPool execution (Invariant #3).");

	uint32_t soa_idx = agent_rids.size();
	SimAgentSlot slot;
	slot.soa_index = soa_idx;
	RID rid = agent_owner.make_rid(slot);
	SimAgentSlot *stored = agent_owner.get_or_null(rid);
	if (stored) {
		stored->self_rid = rid;
	}

	agent_rids.push_back(rid);
	agent_pos_x.push_back(p_pos_x);
	agent_pos_y.push_back(p_pos_y);
	agent_pos_z.push_back(p_pos_z);
	agent_vel.push_back(p_velocity);
	agent_rot_y.push_back(p_rot_y);
	agent_archetype.push_back(p_archetype_id);
	agent_state_flags.push_back(p_state_flags);
	agent_health.push_back(p_health);
	agent_spline_cursor.push_back(0.0f);
	agent_hydration_tier.push_back(TIER_0_ABSTRACT_SOA);
	agent_pin_mask.push_back(PIN_NONE);
	agent_custom_blob.push_back(PackedByteArray());

	agent_rid_to_soa.insert(rid.get_id(), soa_idx);
	return rid;
}

void SimulationServer::free_agent(RID p_agent_rid) {
	ERR_FAIL_COND(!agent_owner.owns(p_agent_rid));
	ERR_FAIL_COND_MSG(WorldState::get_singleton() && WorldState::get_singleton()->is_structure_locked(), "Cannot shrink Agent SoA table during parallel WorkerThreadPool execution (Invariant #3).");

	const uint32_t *idx_ptr = agent_rid_to_soa.getptr(p_agent_rid.get_id());
	if (idx_ptr) {
		uint32_t idx = *idx_ptr;
		uint32_t last_idx = agent_rids.size() - 1;
		if (idx != last_idx) {
			RID moved_rid = agent_rids[last_idx];
			agent_rids[idx] = moved_rid;
			agent_pos_x[idx] = agent_pos_x[last_idx];
			agent_pos_y[idx] = agent_pos_y[last_idx];
			agent_pos_z[idx] = agent_pos_z[last_idx];
			agent_vel[idx] = agent_vel[last_idx];
			agent_rot_y[idx] = agent_rot_y[last_idx];
			agent_archetype[idx] = agent_archetype[last_idx];
			agent_state_flags[idx] = agent_state_flags[last_idx];
			agent_health[idx] = agent_health[last_idx];
			agent_spline_cursor[idx] = agent_spline_cursor[last_idx];
			agent_hydration_tier[idx] = agent_hydration_tier[last_idx];
			agent_pin_mask[idx] = agent_pin_mask[last_idx];
			agent_custom_blob[idx] = agent_custom_blob[last_idx];

			agent_rid_to_soa.insert(moved_rid.get_id(), idx);
			SimAgentSlot *moved_slot = agent_owner.get_or_null(moved_rid);
			if (moved_slot) {
				moved_slot->soa_index = idx;
			}
		}

		agent_rids.remove_at(last_idx);
		agent_pos_x.remove_at(last_idx);
		agent_pos_y.remove_at(last_idx);
		agent_pos_z.remove_at(last_idx);
		agent_vel.remove_at(last_idx);
		agent_rot_y.remove_at(last_idx);
		agent_archetype.remove_at(last_idx);
		agent_state_flags.remove_at(last_idx);
		agent_health.remove_at(last_idx);
		agent_spline_cursor.remove_at(last_idx);
		agent_hydration_tier.remove_at(last_idx);
		agent_pin_mask.remove_at(last_idx);
		agent_custom_blob.remove_at(last_idx);

		agent_rid_to_soa.erase(p_agent_rid.get_id());
	}
	agent_owner.free(p_agent_rid);
}

int32_t SimulationServer::get_agent_soa_index(RID p_agent_rid) const {
	const uint32_t *idx_ptr = agent_rid_to_soa.getptr(p_agent_rid.get_id());
	return idx_ptr ? (int32_t)(*idx_ptr) : -1;
}

void SimulationServer::set_agent_position_64(RID p_agent_rid, double p_x, double p_y, double p_z) {
	int32_t idx = get_agent_soa_index(p_agent_rid);
	ERR_FAIL_COND(idx < 0);
	agent_pos_x[idx] = p_x;
	agent_pos_y[idx] = p_y;
	agent_pos_z[idx] = p_z;
}

void SimulationServer::get_agent_position_64(RID p_agent_rid, double &r_x, double &r_y, double &r_z) const {
	int32_t idx = get_agent_soa_index(p_agent_rid);
	if (idx < 0) {
		r_x = 0.0;
		r_y = 0.0;
		r_z = 0.0;
		return;
	}
	r_x = agent_pos_x[idx];
	r_y = agent_pos_y[idx];
	r_z = agent_pos_z[idx];
}

Vector3 SimulationServer::get_agent_local_position(RID p_agent_rid) const {
	int32_t idx = get_agent_soa_index(p_agent_rid);
	ERR_FAIL_COND_V(idx < 0, Vector3());
	double ox = 0.0, oy = 0.0, oz = 0.0;
	if (WorldState::get_singleton()) {
		WorldState::get_singleton()->get_world_origin_64(ox, oy, oz);
	}
	return Vector3(
			(real_t)(agent_pos_x[idx] - ox),
			(real_t)(agent_pos_y[idx] - oy),
			(real_t)(agent_pos_z[idx] - oz));
}

Transform3D SimulationServer::get_agent_local_transform(RID p_agent_rid) const {
	int32_t idx = get_agent_soa_index(p_agent_rid);
	ERR_FAIL_COND_V(idx < 0, Transform3D());
	Basis b(Vector3(0, 1, 0), agent_rot_y[idx]);
	return Transform3D(b, get_agent_local_position(p_agent_rid));
}

void SimulationServer::set_agent_velocity(RID p_agent_rid, const Vector3 &p_vel) {
	int32_t idx = get_agent_soa_index(p_agent_rid);
	ERR_FAIL_COND(idx < 0);
	agent_vel[idx] = p_vel;
}

Vector3 SimulationServer::get_agent_velocity(RID p_agent_rid) const {
	int32_t idx = get_agent_soa_index(p_agent_rid);
	ERR_FAIL_COND_V(idx < 0, Vector3());
	return agent_vel[idx];
}

void SimulationServer::set_agent_rotation_y(RID p_agent_rid, float p_rot_y) {
	int32_t idx = get_agent_soa_index(p_agent_rid);
	ERR_FAIL_COND(idx < 0);
	agent_rot_y[idx] = p_rot_y;
}

float SimulationServer::get_agent_rotation_y(RID p_agent_rid) const {
	int32_t idx = get_agent_soa_index(p_agent_rid);
	ERR_FAIL_COND_V(idx < 0, 0.0f);
	return agent_rot_y[idx];
}

StringName SimulationServer::get_agent_archetype(RID p_agent_rid) const {
	int32_t idx = get_agent_soa_index(p_agent_rid);
	ERR_FAIL_COND_V(idx < 0, StringName());
	return agent_archetype[idx];
}

void SimulationServer::set_agent_state_flags(RID p_agent_rid, uint32_t p_flags) {
	int32_t idx = get_agent_soa_index(p_agent_rid);
	ERR_FAIL_COND(idx < 0);
	agent_state_flags[idx] = p_flags;
}

uint32_t SimulationServer::get_agent_state_flags(RID p_agent_rid) const {
	int32_t idx = get_agent_soa_index(p_agent_rid);
	ERR_FAIL_COND_V(idx < 0, 0);
	return agent_state_flags[idx];
}

void SimulationServer::set_agent_health(RID p_agent_rid, float p_health) {
	int32_t idx = get_agent_soa_index(p_agent_rid);
	ERR_FAIL_COND(idx < 0);
	agent_health[idx] = p_health;
}

float SimulationServer::get_agent_health(RID p_agent_rid) const {
	int32_t idx = get_agent_soa_index(p_agent_rid);
	ERR_FAIL_COND_V(idx < 0, 0.0f);
	return agent_health[idx];
}

void SimulationServer::set_agent_spline_cursor(RID p_agent_rid, float p_cursor) {
	int32_t idx = get_agent_soa_index(p_agent_rid);
	ERR_FAIL_COND(idx < 0);
	agent_spline_cursor[idx] = p_cursor;
}

float SimulationServer::get_agent_spline_cursor(RID p_agent_rid) const {
	int32_t idx = get_agent_soa_index(p_agent_rid);
	ERR_FAIL_COND_V(idx < 0, 0.0f);
	return agent_spline_cursor[idx];
}

void SimulationServer::set_agent_hydration_tier(RID p_agent_rid, uint8_t p_tier) {
	int32_t idx = get_agent_soa_index(p_agent_rid);
	ERR_FAIL_COND(idx < 0);
	agent_hydration_tier[idx] = p_tier;
}

uint8_t SimulationServer::get_agent_hydration_tier(RID p_agent_rid) const {
	int32_t idx = get_agent_soa_index(p_agent_rid);
	ERR_FAIL_COND_V(idx < 0, TIER_0_ABSTRACT_SOA);
	return agent_hydration_tier[idx];
}

void SimulationServer::set_agent_pin_mask(RID p_agent_rid, uint32_t p_pin_mask) {
	int32_t idx = get_agent_soa_index(p_agent_rid);
	ERR_FAIL_COND(idx < 0);
	agent_pin_mask[idx] = p_pin_mask;
}

uint32_t SimulationServer::get_agent_pin_mask(RID p_agent_rid) const {
	int32_t idx = get_agent_soa_index(p_agent_rid);
	ERR_FAIL_COND_V(idx < 0, 0);
	return agent_pin_mask[idx];
}

bool SimulationServer::is_agent_pinned(RID p_agent_rid) const {
	return get_agent_pin_mask(p_agent_rid) != PIN_NONE;
}

void SimulationServer::set_agent_custom_blob(RID p_agent_rid, const PackedByteArray &p_blob) {
	int32_t idx = get_agent_soa_index(p_agent_rid);
	ERR_FAIL_COND(idx < 0);
	agent_custom_blob[idx] = p_blob;
}

PackedByteArray SimulationServer::get_agent_custom_blob(RID p_agent_rid) const {
	int32_t idx = get_agent_soa_index(p_agent_rid);
	ERR_FAIL_COND_V(idx < 0, PackedByteArray());
	return agent_custom_blob[idx];
}

PackedByteArray SimulationServer::serialize_agent_soa_blob() const {
	Ref<StreamPeerBuffer> spb;
	spb.instantiate();
	uint32_t count = agent_rids.size();
	spb->put_u32(count);
	for (uint32_t i = 0; i < count; i++) {
		spb->put_utf8_string(String(agent_archetype[i]));
		spb->put_double(agent_pos_x[i]);
		spb->put_double(agent_pos_y[i]);
		spb->put_double(agent_pos_z[i]);
		spb->put_float(agent_vel[i].x);
		spb->put_float(agent_vel[i].y);
		spb->put_float(agent_vel[i].z);
		spb->put_float(agent_rot_y[i]);
		spb->put_u32(agent_state_flags[i]);
		spb->put_float(agent_health[i]);
		spb->put_float(agent_spline_cursor[i]);
		spb->put_u32(agent_pin_mask[i]);
		const PackedByteArray &custom = agent_custom_blob[i];
		spb->put_u32(custom.size());
		if (!custom.is_empty()) {
			spb->put_data(custom.ptr(), custom.size());
		}
	}
	return spb->get_data_array();
}

Error SimulationServer::deserialize_agent_soa_blob(const PackedByteArray &p_blob) {
	if (p_blob.is_empty()) {
		return OK;
	}
	if (p_blob.size() < 4) {
		return ERR_INVALID_DATA;
	}
	Ref<StreamPeerBuffer> spb;
	spb.instantiate();
	spb->set_data_array(p_blob);

	uint32_t count = spb->get_u32();
	// Reconcile existing agent slots if count matches (preserves live RIDs across save/load and rewind!).
	bool reuse_existing_rids = (count == agent_rids.size());
	if (!reuse_existing_rids) {
		while (!agent_rids.is_empty()) {
			free_agent(agent_rids[agent_rids.size() - 1]);
		}
	}

	for (uint32_t i = 0; i < count; i++) {
		if (spb->get_available_bytes() < 48) {
			return ERR_INVALID_DATA;
		}
		StringName arch(spb->get_utf8_string());
		double px = spb->get_double();
		double py = spb->get_double();
		double pz = spb->get_double();
		float vx = spb->get_float();
		float vy = spb->get_float();
		float vz = spb->get_float();
		float rot_y = spb->get_float();
		uint32_t flags = spb->get_u32();
		float health = spb->get_float();
		float spline = spb->get_float();
		uint32_t pin = spb->get_u32();
		uint32_t blob_len = spb->get_u32();
		if (blob_len > (uint32_t)spb->get_available_bytes()) {
			return ERR_INVALID_DATA;
		}
		PackedByteArray custom;
		if (blob_len > 0) {
			custom.resize(blob_len);
			spb->get_data(custom.ptrw(), blob_len);
		}

		if (reuse_existing_rids) {
			agent_archetype[i] = arch;
			agent_pos_x[i] = px;
			agent_pos_y[i] = py;
			agent_pos_z[i] = pz;
			agent_vel[i] = Vector3(vx, vy, vz);
			agent_rot_y[i] = rot_y;
			agent_state_flags[i] = flags;
			agent_health[i] = health;
			agent_spline_cursor[i] = spline;
			agent_pin_mask[i] = pin;
			agent_custom_blob[i] = custom;
		} else {
			RID r = create_agent(arch, px, py, pz, Vector3(vx, vy, vz), rot_y, health, flags);
			set_agent_spline_cursor(r, spline);
			set_agent_pin_mask(r, pin);
			set_agent_custom_blob(r, custom);
		}
	}
	return OK;
}

void SimulationServer::set_snapshot_interval_ticks(uint32_t p_interval) {
	ERR_FAIL_COND(p_interval == 0);
	snapshot_interval_ticks = p_interval;
}

void SimulationServer::_capture_rewind_snapshot_if_due(uint64_t p_tick_index) {
	if (!rewind_capture_enabled || snapshot_interval_ticks == 0) {
		return;
	}
	if ((p_tick_index % snapshot_interval_ticks) == 0) {
		capture_rewind_snapshot_now();
	}
}

void SimulationServer::capture_rewind_snapshot_now() {
	if (!SandboxSerializer::get_singleton()) {
		return;
	}
	RewindSnapshot snap;
	snap.tick_index = SandboxClock::get_singleton() ? SandboxClock::get_singleton()->get_sim_tick_index() : 0;
	snap.sim_time_elapsed = SandboxClock::get_singleton() ? SandboxClock::get_singleton()->get_sim_time_elapsed() : 0.0;
	snap.binary_data = SandboxSerializer::get_singleton()->save_to_binary();

	if (rewind_ring.size() < max_rewind_snapshots) {
		rewind_ring.push_back(snap);
		snapshot_write_cursor = rewind_ring.size() % max_rewind_snapshots;
	} else {
		rewind_ring[snapshot_write_cursor] = snap;
		snapshot_write_cursor = (snapshot_write_cursor + 1) % max_rewind_snapshots;
	}
}

void SimulationServer::clear_rewind_snapshots() {
	rewind_ring.clear();
	snapshot_write_cursor = 0;
}

Error SimulationServer::rewind(uint64_t p_ticks_back) {
	if (rewind_ring.is_empty() || !SandboxSerializer::get_singleton()) {
		return ERR_UNAVAILABLE;
	}
	uint64_t current_tick = SandboxClock::get_singleton() ? SandboxClock::get_singleton()->get_sim_tick_index() : 0;
	uint64_t target_tick = (p_ticks_back >= current_tick) ? 0 : (current_tick - p_ticks_back);

	int best_idx = -1;
	uint64_t best_diff = UINT64_MAX;
	for (uint32_t i = 0; i < rewind_ring.size(); i++) {
		uint64_t diff = (rewind_ring[i].tick_index > target_tick)
				? (rewind_ring[i].tick_index - target_tick)
				: (target_tick - rewind_ring[i].tick_index);
		if (diff < best_diff) {
			best_diff = diff;
			best_idx = (int)i;
		}
	}
	if (best_idx < 0) {
		return ERR_DOES_NOT_EXIST;
	}
	return SandboxSerializer::get_singleton()->load_from_binary(rewind_ring[best_idx].binary_data);
}

void SimulationServer::_on_clock_sim_step(double p_sim_delta, uint64_t p_tick_index) {
	if (singleton) {
		singleton->step(p_sim_delta, p_tick_index);
	}
}

void SimulationServer::_on_serializer_write_sim_modules(Dictionary &r_modules) {
	if (!singleton) {
		return;
	}
	for (uint32_t i = 0; i < singleton->registered_module_rids.size(); i++) {
		SimModuleEntry *entry = singleton->module_owner.get_or_null(singleton->registered_module_rids[i]);
		if (entry && entry->module.is_valid() && entry->module->get_module_id() != StringName()) {
			PackedByteArray blob = entry->module->on_snapshot_write();
			if (!blob.is_empty()) {
				r_modules[entry->module->get_module_id()] = blob;
			} else {
				r_modules.erase(entry->module->get_module_id());
			}
		}
	}
	if (!singleton->agent_rids.is_empty()) {
		r_modules[SNAME("traffic_soa")] = singleton->serialize_agent_soa_blob();
	} else {
		r_modules.erase(SNAME("traffic_soa"));
	}
}

Error SimulationServer::_on_serializer_read_sim_modules(const Dictionary &p_section) {
	if (!singleton) {
		return OK;
	}
	for (uint32_t i = 0; i < singleton->registered_module_rids.size(); i++) {
		SimModuleEntry *entry = singleton->module_owner.get_or_null(singleton->registered_module_rids[i]);
		if (entry && entry->module.is_valid()) {
			StringName mid = entry->module->get_module_id();
			if (p_section.has(mid)) {
				entry->module->on_snapshot_read(p_section[mid]);
			} else if (p_section.has(String(mid))) {
				entry->module->on_snapshot_read(p_section[String(mid)]);
			}
		}
	}
	if (p_section.has(SNAME("traffic_soa"))) {
		return singleton->deserialize_agent_soa_blob(p_section[SNAME("traffic_soa")]);
	} else if (p_section.has("traffic_soa")) {
		return singleton->deserialize_agent_soa_blob(p_section["traffic_soa"]);
	}
	return OK;
}

Variant SimulationServer::_cmd_sim_rewind(int64_t p_ticks) {
	Error err = rewind((uint64_t)MAX((int64_t)0, p_ticks));
	if (err != OK) {
		Dictionary d;
		d["ok"] = false;
		d["output"] = "Rewind failed: no snapshot available.";
		d["error_code"] = (int)err;
		return d;
	}
	return vformat("Rewound simulation to tick %d.", SandboxClock::get_singleton() ? (int)SandboxClock::get_singleton()->get_sim_tick_index() : 0);
}

Variant SimulationServer::_cmd_sim_stats() {
	return vformat("Modules: %d | Agents (SoA): %d | Tasks Dispatched/Awaited: %d/%d | Snapshots: %d",
			(int)get_module_count(),
			(int)get_agent_count(),
			(int)get_total_tasks_dispatched(),
			(int)get_total_tasks_awaited(),
			(int)get_rewind_snapshot_count());
}

void SimulationServer::bind_core_hooks() {
	if (SandboxClock::get_singleton()) {
		SandboxClock::get_singleton()->set_sim_step_callback(&SimulationServer::_on_clock_sim_step);
	}
	if (SandboxSerializer::get_singleton()) {
		SandboxSerializer::get_singleton()->set_sim_modules_callbacks(
				&SimulationServer::_on_serializer_write_sim_modules,
				&SimulationServer::_on_serializer_read_sim_modules);
	}
	if (CommandBus::get_singleton()) {
		Vector<Variant::Type> sig_int;
		sig_int.push_back(Variant::INT);
		CommandBus::get_singleton()->register_command_native(
				SNAME("sim.rewind"),
				CommandBus::PERMISSION_ADMIN,
				sig_int,
				callable_mp(this, &SimulationServer::_cmd_sim_rewind),
				"Rewind SimulationServer by N simulation ticks.");
		CommandBus::get_singleton()->register_command_native(
				SNAME("sim.stats"),
				CommandBus::PERMISSION_USER,
				Vector<Variant::Type>(),
				callable_mp(this, &SimulationServer::_cmd_sim_stats),
				"Display SimulationServer module, SoA agent, and thread-pool task metrics.");
	}
}

void SimulationServer::clear() {
	while (!registered_module_rids.is_empty()) {
		unregister_module(registered_module_rids[registered_module_rids.size() - 1]);
	}
	while (!agent_rids.is_empty()) {
		free_agent(agent_rids[agent_rids.size() - 1]);
	}
	clear_rewind_snapshots();
	total_tasks_dispatched.store(0, std::memory_order_release);
	total_tasks_awaited.store(0, std::memory_order_release);
	nested_wait_violations.store(0, std::memory_order_release);
}

void SimulationServer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("register_module", "module"), &SimulationServer::register_module);
	ClassDB::bind_method(D_METHOD("unregister_module", "module_rid"), &SimulationServer::unregister_module);
	ClassDB::bind_method(D_METHOD("has_module", "module_rid"), &SimulationServer::has_module);
	ClassDB::bind_method(D_METHOD("get_module_count"), &SimulationServer::get_module_count);
	ClassDB::bind_method(D_METHOD("get_module_metrics", "module_rid"), &SimulationServer::get_module_metrics);
	ClassDB::bind_method(D_METHOD("step", "sim_delta", "tick_index"), &SimulationServer::step, DEFVAL(0));

	ClassDB::bind_method(D_METHOD("get_total_tasks_dispatched"), &SimulationServer::get_total_tasks_dispatched);
	ClassDB::bind_method(D_METHOD("get_total_tasks_awaited"), &SimulationServer::get_total_tasks_awaited);
	ClassDB::bind_method(D_METHOD("get_leaked_task_count"), &SimulationServer::get_leaked_task_count);
	ClassDB::bind_method(D_METHOD("get_nested_wait_violations"), &SimulationServer::get_nested_wait_violations);

	ClassDB::bind_method(D_METHOD("create_agent", "archetype_id", "pos_x", "pos_y", "pos_z", "velocity", "rot_y", "health", "state_flags"), &SimulationServer::create_agent, DEFVAL(0.0), DEFVAL(0.0), DEFVAL(0.0), DEFVAL(Vector3()), DEFVAL(0.0f), DEFVAL(100.0f), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("free_agent", "agent_rid"), &SimulationServer::free_agent);
	ClassDB::bind_method(D_METHOD("has_agent", "agent_rid"), &SimulationServer::has_agent);
	ClassDB::bind_method(D_METHOD("get_agent_count"), &SimulationServer::get_agent_count);
	ClassDB::bind_method(D_METHOD("get_agent_soa_index", "agent_rid"), &SimulationServer::get_agent_soa_index);
	ClassDB::bind_method(D_METHOD("get_agent_rid_at", "soa_index"), &SimulationServer::get_agent_rid_at);

	ClassDB::bind_method(D_METHOD("set_agent_position_64", "agent_rid", "x", "y", "z"), &SimulationServer::set_agent_position_64);
	ClassDB::bind_method(D_METHOD("get_agent_local_position", "agent_rid"), &SimulationServer::get_agent_local_position);
	ClassDB::bind_method(D_METHOD("get_agent_local_transform", "agent_rid"), &SimulationServer::get_agent_local_transform);
	ClassDB::bind_method(D_METHOD("set_agent_velocity", "agent_rid", "vel"), &SimulationServer::set_agent_velocity);
	ClassDB::bind_method(D_METHOD("get_agent_velocity", "agent_rid"), &SimulationServer::get_agent_velocity);
	ClassDB::bind_method(D_METHOD("set_agent_rotation_y", "agent_rid", "rot_y"), &SimulationServer::set_agent_rotation_y);
	ClassDB::bind_method(D_METHOD("get_agent_rotation_y", "agent_rid"), &SimulationServer::get_agent_rotation_y);
	ClassDB::bind_method(D_METHOD("get_agent_archetype", "agent_rid"), &SimulationServer::get_agent_archetype);
	ClassDB::bind_method(D_METHOD("set_agent_state_flags", "agent_rid", "flags"), &SimulationServer::set_agent_state_flags);
	ClassDB::bind_method(D_METHOD("get_agent_state_flags", "agent_rid"), &SimulationServer::get_agent_state_flags);
	ClassDB::bind_method(D_METHOD("set_agent_health", "agent_rid", "health"), &SimulationServer::set_agent_health);
	ClassDB::bind_method(D_METHOD("get_agent_health", "agent_rid"), &SimulationServer::get_agent_health);
	ClassDB::bind_method(D_METHOD("set_agent_spline_cursor", "agent_rid", "cursor"), &SimulationServer::set_agent_spline_cursor);
	ClassDB::bind_method(D_METHOD("get_agent_spline_cursor", "agent_rid"), &SimulationServer::get_agent_spline_cursor);
	ClassDB::bind_method(D_METHOD("set_agent_hydration_tier", "agent_rid", "tier"), &SimulationServer::set_agent_hydration_tier);
	ClassDB::bind_method(D_METHOD("get_agent_hydration_tier", "agent_rid"), &SimulationServer::get_agent_hydration_tier);
	ClassDB::bind_method(D_METHOD("set_agent_pin_mask", "agent_rid", "pin_mask"), &SimulationServer::set_agent_pin_mask);
	ClassDB::bind_method(D_METHOD("get_agent_pin_mask", "agent_rid"), &SimulationServer::get_agent_pin_mask);
	ClassDB::bind_method(D_METHOD("is_agent_pinned", "agent_rid"), &SimulationServer::is_agent_pinned);
	ClassDB::bind_method(D_METHOD("set_agent_custom_blob", "agent_rid", "blob"), &SimulationServer::set_agent_custom_blob);
	ClassDB::bind_method(D_METHOD("get_agent_custom_blob", "agent_rid"), &SimulationServer::get_agent_custom_blob);

	ClassDB::bind_method(D_METHOD("set_rewind_capture_enabled", "enabled"), &SimulationServer::set_rewind_capture_enabled);
	ClassDB::bind_method(D_METHOD("is_rewind_capture_enabled"), &SimulationServer::is_rewind_capture_enabled);
	ClassDB::bind_method(D_METHOD("set_snapshot_interval_ticks", "interval"), &SimulationServer::set_snapshot_interval_ticks);
	ClassDB::bind_method(D_METHOD("get_snapshot_interval_ticks"), &SimulationServer::get_snapshot_interval_ticks);
	ClassDB::bind_method(D_METHOD("capture_rewind_snapshot_now"), &SimulationServer::capture_rewind_snapshot_now);
	ClassDB::bind_method(D_METHOD("get_rewind_snapshot_count"), &SimulationServer::get_rewind_snapshot_count);
	ClassDB::bind_method(D_METHOD("clear_rewind_snapshots"), &SimulationServer::clear_rewind_snapshots);
	ClassDB::bind_method(D_METHOD("rewind", "ticks_back"), &SimulationServer::rewind);
	ClassDB::bind_method(D_METHOD("clear"), &SimulationServer::clear);
}

SimulationServer::SimulationServer() {
	singleton = this;
	module_owner.set_description("SimModuleEntry");
	agent_owner.set_description("SimAgentSlot");
}

SimulationServer::~SimulationServer() {
	clear();
	if (singleton == this) {
		singleton = nullptr;
	}
}
