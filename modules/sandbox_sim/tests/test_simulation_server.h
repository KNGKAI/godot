/**************************************************************************/
/*  test_simulation_server.h                                              */
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

#include "../sim_module.h"
#include "../simulation_server.h"

#include "modules/sandbox_core/event_bus.h"
#include "modules/sandbox_core/sandbox_clock.h"
#include "modules/sandbox_core/world_state.h"

#include "core/io/stream_peer.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "tests/test_macros.h"

namespace TestSimulationServer {

#ifdef THREADS_ENABLED
class StressTestSimModule : public SimModule {
	GDCLASS(StressTestSimModule, SimModule);

public:
	uint32_t channel_index = 0;
	int64_t internal_counter = 0;
	std::atomic<int> *first_tick_arrivals = nullptr;
	int expected_overlap = 0;
	bool overlap_observed = false;
	int64_t worker_ticks = 0;
	int milestones_received = 0;
	bool milestones_valid = true;

	void receive_milestone(const StringName &p_id, const Dictionary &p_payload) {
		if (StringName(p_payload["module_id"]) == get_module_id()) {
			milestones_received++;
			milestones_valid &= Thread::is_main_thread() && int64_t(p_payload["ticks"]) == 2500 * milestones_received;
		}
	}

	virtual void on_init(WorldState *p_state, EventBus *p_events) override {
		if (p_state) {
			channel_index = (uint32_t)p_state->register_float_channel(get_module_id(), 0.0);
		}
	}

	virtual void on_tick(double p_delta_sim, WorldState *p_state, EventBus *p_events) override {
		internal_counter++;
		if (WorkerThreadPool::get_singleton()->get_caller_task_id() != WorkerThreadPool::INVALID_TASK_ID) {
			worker_ticks++;
		}
		if (internal_counter == 1) {
			// Hold the first wave briefly so even very short ticks demonstrate simultaneous execution.
			first_tick_arrivals->fetch_add(1);
			OS *os = OS::get_singleton();
			const uint64_t deadline = os->get_ticks_usec() + 2000000;
			while (first_tick_arrivals->load() < expected_overlap && os->get_ticks_usec() < deadline) {
				os->delay_usec(100);
			}
			overlap_observed = first_tick_arrivals->load() >= expected_overlap;
		}
		if (p_state) {
			// Direct zero-boxing SoA float column update (Invariant #3: container is pre-allocated & locked).
			double *col = p_state->get_float_column_ptr();
			if (col) {
				col[channel_index] += p_delta_sim;
			}
		}
		// Emit periodic threshold events from worker thread (Invariant #4: auto-deferred to main thread).
		if ((internal_counter % 2500) == 0 && p_events) {
			Dictionary d;
			d["module_id"] = get_module_id();
			d["ticks"] = internal_counter;
			p_events->emit(SNAME("sim_milestone"), d);
		}
	}

	virtual PackedByteArray on_snapshot_write() const override {
		Ref<StreamPeerBuffer> spb;
		spb.instantiate();
		spb->put_64(internal_counter);
		return spb->get_data_array();
	}

	virtual void on_snapshot_read(const PackedByteArray &p_data) override {
		if (p_data.size() >= 8) {
			Ref<StreamPeerBuffer> spb;
			spb.instantiate();
			spb->set_data_array(p_data);
			internal_counter = spb->get_64();
		}
	}
};

TEST_CASE("[SandboxSim][Gate2] 10,000 Consecutive Ticks Across 8 Concurrent SimModules on WorkerThreadPool") {
	SimulationServer *sim = SimulationServer::get_singleton();
	WorldState *ws = WorldState::get_singleton();
	EventBus *eb = EventBus::get_singleton();
	REQUIRE(sim != nullptr);
	REQUIRE(ws != nullptr);
	REQUIRE(eb != nullptr);

	sim->clear();
	ws->clear();
	eb->clear_all();
	sim->bind_core_hooks();

	constexpr int NUM_MODULES = 8;
	constexpr int TOTAL_TICKS = 10000;
	const double step_dt = 0.01; // 100 Hz tick rate
	WorkerThreadPool *pool = WorkerThreadPool::get_singleton();
	REQUIRE(pool != nullptr);
	const int expected_overlap = MIN(NUM_MODULES, pool->get_thread_count());
	CAPTURE(expected_overlap);
	std::atomic<int> first_tick_arrivals{ 0 };

	Ref<StressTestSimModule> mods[NUM_MODULES];
	RID mod_rids[NUM_MODULES];

	for (int i = 0; i < NUM_MODULES; i++) {
		mods[i].instantiate();
		mods[i]->set_module_id(StringName(vformat("stress_mod_%d", i)));
		mods[i]->set_tick_rate_hz(100.0);
		mods[i]->set_priority(0);
		mods[i]->set_thread_safe_tick(true);
		mods[i]->first_tick_arrivals = &first_tick_arrivals;
		mods[i]->expected_overlap = expected_overlap;
		mod_rids[i] = sim->register_module(mods[i]);
		REQUIRE(mod_rids[i].is_valid());
		eb->subscribe(SNAME("sim_milestone"), callable_mp(mods[i].ptr(), &StressTestSimModule::receive_milestone));
	}

	// Execute 10,000 consecutive ticks across the 8 concurrent modules (80,000 worker task executions).
	for (int tick = 1; tick <= TOTAL_TICKS; tick++) {
		sim->step(step_dt, (uint64_t)tick);
	}

	// Gate 2 Verification:
	// 1. Zero leaked task handles (every dispatched WorkerThreadPool task was strictly awaited).
	CHECK(sim->get_leaked_task_count() == 0);
	CHECK(sim->get_total_tasks_dispatched() == NUM_MODULES * TOTAL_TICKS);
	CHECK(sim->get_total_tasks_dispatched() == sim->get_total_tasks_awaited());
	// 2. Zero nested wait / ERR_BUSY violations.
	CHECK(sim->get_nested_wait_violations() == 0);
	// 3. Every module executed all 10,000 ticks with exact deterministic SoA accumulation.
	for (int i = 0; i < NUM_MODULES; i++) {
		CHECK(mods[i]->internal_counter == TOTAL_TICKS);
		CHECK(mods[i]->worker_ticks == TOTAL_TICKS);
		CHECK(mods[i]->overlap_observed);
		CHECK(mods[i]->milestones_received == TOTAL_TICKS / 2500);
		CHECK(mods[i]->milestones_valid);
		double val = ws->get_float(mods[i]->get_module_id());
		CHECK(Math::is_equal_approx(val, TOTAL_TICKS * step_dt, 0.01));
	}
	// 4. All worker-emitted deferred events were flushed to the main thread cleanly.
	CHECK(eb->get_pending_deferred_count() == 0);

	sim->clear();
	ws->clear();
	eb->clear_all();
}
#endif // THREADS_ENABLED

TEST_CASE("[SandboxSim] Agent SoA Table & Deterministic Rewind Ring Buffer") {
	SimulationServer *sim = SimulationServer::get_singleton();
	WorldState *ws = WorldState::get_singleton();
	SandboxClock *clock = SandboxClock::get_singleton();
	REQUIRE(sim != nullptr);
	REQUIRE(ws != nullptr);
	REQUIRE(clock != nullptr);

	sim->clear();
	ws->clear();
	clock->reset();
	sim->bind_core_hooks();

	// Enable rewind snapshots every 5 ticks.
	sim->set_rewind_capture_enabled(true);
	sim->set_snapshot_interval_ticks(5);

	RID agent_a = sim->create_agent(SNAME("sedan_01"), 100.0, 0.0, 200.0, Vector3(10.0f, 0.0f, 0.0f), 0.0f, 95.0f, 1);
	RID agent_b = sim->create_agent(SNAME("pedestrian_01"), -50.0, 0.0, 10.0, Vector3(0.0f, 0.0f, 2.0f), 1.57f, 100.0f, 0);
	CHECK(sim->has_agent(agent_a));
	CHECK(sim->has_agent(agent_b));
	CHECK(sim->get_agent_count() == 2);

	// Step 10 ticks at dt = 1.0s (captures snapshots at tick 5 and tick 10).
	clock->set_sim_fixed_step(1.0);
	clock->step_immediate(10);

	CHECK(sim->get_rewind_snapshot_count() >= 2);
	Vector3 pos_a_t10 = sim->get_agent_local_position(agent_a);
	CHECK(Math::is_equal_approx(pos_a_t10.x, 200.0f, 0.1f)); // 100 + 10 * 10

	// Mutate health at tick 10, then rewind 5 ticks back to tick 5.
	sim->set_agent_health(agent_a, 10.0f);
	CHECK(sim->rewind(5) == OK);
	CHECK(clock->get_sim_tick_index() == 5);
	CHECK(Math::is_equal_approx(sim->get_agent_health(agent_a), 95.0f));
	Vector3 pos_a_t5 = sim->get_agent_local_position(agent_a);
	CHECK(Math::is_equal_approx(pos_a_t5.x, 150.0f, 0.1f)); // 100 + 5 * 10

	sim->set_rewind_capture_enabled(false);
	sim->clear();
	ws->clear();
	clock->reset();
}

} // namespace TestSimulationServer
