/**************************************************************************/
/*  sandbox_clock.h                                                       */
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

#include "core/object/class_db.h"
#include "core/object/object.h"

class SandboxClock : public Object {
	GDCLASS(SandboxClock, Object);

public:
	typedef void (*SimStepCallback)(double p_sim_delta, uint64_t p_tick_index);
	typedef void (*FrameUpdateCallback)(double p_delta);
	typedef void (*PhysicsStartCallback)(double p_physics_delta);

private:
	static inline SandboxClock *singleton = nullptr;

	// 1. Simulation Clock (sim_clock)
	bool sim_paused = false;
	double sim_time_scale = 1.0; // Clamped [0.0, 100.0]
	double sim_fixed_step = 0.05; // 20 Hz default fixed step
	double sim_accumulator = 0.0;
	uint64_t sim_tick_index = 0;
	double sim_time_elapsed = 0.0;
	int pending_manual_steps = 0;

	// Spiral-of-death frame budget caps (Finding 4)
	int max_sim_steps_per_frame = 64;
	uint64_t max_sim_frame_budget_usec = 8000; // 8.0 ms cap to preserve 60 Hz physics & < 16.6 ms UI
	uint64_t last_sim_frame_usec = 0;
	int last_sim_steps_executed = 0;

	// 2. Physics & World Clock (physics_clock)
	double physics_step_delta = 1.0 / 60.0;
	double physics_time_elapsed = 0.0;
	uint64_t physics_frame_count = 0;
	int last_physics_steps_executed = 0;

	// 3. Narrative & UI Clock (narrative_ui_clock)
	bool narrative_paused = false;
	double narrative_time_scale = 1.0;
	double narrative_delta = 0.0;
	double narrative_time_elapsed = 0.0;
	double ui_unscaled_delta = 0.0;
	double ui_time_elapsed = 0.0;

	// Decoupled subsystem callbacks registered by Tier 2 modules
	SimStepCallback sim_step_callback = nullptr;
	PhysicsStartCallback physics_start_callback = nullptr;
	FrameUpdateCallback narrative_update_callback = nullptr;
	FrameUpdateCallback hydration_update_callback = nullptr;
	FrameUpdateCallback streaming_update_callback = nullptr;

	void _execute_single_sim_step();

protected:
	static void _bind_methods();

public:
	static SandboxClock *get_singleton() { return singleton; }

	// Simulation Clock API
	void set_sim_paused(bool p_paused);
	bool is_sim_paused() const { return sim_paused; }

	void set_sim_time_scale(double p_scale);
	double get_sim_time_scale() const { return sim_time_scale; }

	void set_sim_fixed_step(double p_step);
	double get_sim_fixed_step() const { return sim_fixed_step; }

	void set_sim_tick_rate_hz(double p_hz);
	double get_sim_tick_rate_hz() const { return sim_fixed_step > 0.0 ? (1.0 / sim_fixed_step) : 0.0; }

	uint64_t get_sim_tick_index() const { return sim_tick_index; }
	double get_sim_time_elapsed() const { return sim_time_elapsed; }
	void set_sim_clock_state(uint64_t p_tick_index, double p_time_elapsed);

	void step(int p_steps = 1);
	void step_immediate(int p_steps = 1);

	void set_max_sim_steps_per_frame(int p_max_steps);
	int get_max_sim_steps_per_frame() const { return max_sim_steps_per_frame; }

	void set_max_sim_frame_budget_usec(uint64_t p_budget_usec);
	uint64_t get_max_sim_frame_budget_usec() const { return max_sim_frame_budget_usec; }

	uint64_t get_last_sim_frame_usec() const { return last_sim_frame_usec; }
	int get_last_sim_steps_executed() const { return last_sim_steps_executed; }

	// Physics Clock API
	double get_physics_step_delta() const { return physics_step_delta; }
	double get_physics_time_elapsed() const { return physics_time_elapsed; }
	uint64_t get_physics_frame_count() const { return physics_frame_count; }
	int get_last_physics_steps_executed() const { return last_physics_steps_executed; }

	// Narrative & UI Clock API
	void set_narrative_paused(bool p_paused);
	bool is_narrative_paused() const { return narrative_paused; }

	void set_narrative_time_scale(double p_scale);
	double get_narrative_time_scale() const { return narrative_time_scale; }

	double get_narrative_delta() const { return narrative_delta; }
	double get_narrative_time_elapsed() const { return narrative_time_elapsed; }
	double get_ui_unscaled_delta() const { return ui_unscaled_delta; }
	double get_ui_time_elapsed() const { return ui_time_elapsed; }

	// Main-loop integration hooks (called by main/main.cpp)
	void notify_physics_step_start(double p_physics_delta);
	void advance(double p_unscaled_process_step, double p_physics_step, int p_physics_steps);
	void reset();

	// Subsystem callback registration
	void set_sim_step_callback(SimStepCallback p_cb) { sim_step_callback = p_cb; }
	void set_physics_start_callback(PhysicsStartCallback p_cb) { physics_start_callback = p_cb; }
	void set_narrative_update_callback(FrameUpdateCallback p_cb) { narrative_update_callback = p_cb; }
	void set_hydration_update_callback(FrameUpdateCallback p_cb) { hydration_update_callback = p_cb; }
	void set_streaming_update_callback(FrameUpdateCallback p_cb) { streaming_update_callback = p_cb; }

	SandboxClock();
	~SandboxClock();
};
