/**************************************************************************/
/*  sandbox_clock.cpp                                                     */
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

#include "sandbox_clock.h"

#include "core/math/math_funcs.h"
#include "core/os/os.h"

void SandboxClock::set_sim_paused(bool p_paused) {
	sim_paused = p_paused;
}

void SandboxClock::set_sim_time_scale(double p_scale) {
	sim_time_scale = CLAMP(p_scale, 0.0, 100.0);
}

void SandboxClock::set_sim_fixed_step(double p_step) {
	ERR_FAIL_COND_MSG(p_step <= 0.0001, "SandboxClock: sim_fixed_step must be > 0.0001 seconds.");
	sim_fixed_step = p_step;
}

void SandboxClock::set_sim_tick_rate_hz(double p_hz) {
	ERR_FAIL_COND_MSG(p_hz <= 0.0, "SandboxClock: sim_tick_rate_hz must be > 0.0.");
	sim_fixed_step = 1.0 / p_hz;
}

void SandboxClock::set_sim_clock_state(uint64_t p_tick_index, double p_time_elapsed) {
	sim_tick_index = p_tick_index;
	sim_time_elapsed = p_time_elapsed;
	sim_accumulator = 0.0;
}

void SandboxClock::step(int p_steps) {
	if (p_steps > 0) {
		pending_manual_steps += p_steps;
	}
}

void SandboxClock::step_immediate(int p_steps) {
	ERR_FAIL_COND(p_steps <= 0);
	uint64_t start_usec = OS::get_singleton() ? OS::get_singleton()->get_ticks_usec() : 0;
	for (int i = 0; i < p_steps; i++) {
		_execute_single_sim_step();
	}
	last_sim_steps_executed = p_steps;
	if (OS::get_singleton()) {
		last_sim_frame_usec = OS::get_singleton()->get_ticks_usec() - start_usec;
	}
}

void SandboxClock::set_max_sim_steps_per_frame(int p_max_steps) {
	ERR_FAIL_COND(p_max_steps < 1);
	max_sim_steps_per_frame = p_max_steps;
}

void SandboxClock::set_max_sim_frame_budget_usec(uint64_t p_budget_usec) {
	ERR_FAIL_COND(p_budget_usec < 100);
	max_sim_frame_budget_usec = p_budget_usec;
}

void SandboxClock::set_narrative_paused(bool p_paused) {
	narrative_paused = p_paused;
}

void SandboxClock::set_narrative_time_scale(double p_scale) {
	narrative_time_scale = CLAMP(p_scale, 0.0, 10.0);
}

void SandboxClock::_execute_single_sim_step() {
	sim_tick_index++;
	sim_time_elapsed += sim_fixed_step;
	if (sim_step_callback) {
		sim_step_callback(sim_fixed_step, sim_tick_index);
	}
}

void SandboxClock::notify_physics_step_start(double p_physics_delta) {
	physics_step_delta = p_physics_delta;
	physics_time_elapsed += p_physics_delta;
	physics_frame_count++;
	if (physics_start_callback) {
		physics_start_callback(p_physics_delta);
	}
}

void SandboxClock::advance(double p_unscaled_process_step, double p_physics_step, int p_physics_steps) {
	// 1. Update Physics Clock metrics (completely isolated from sim_clock scale/pause).
	if (p_physics_step > 0.0) {
		physics_step_delta = p_physics_step;
	}
	last_physics_steps_executed = p_physics_steps;

	// 2. Update Narrative & UI Clock (unscaled real time for responsive 3D terminals & HUDs).
	ui_unscaled_delta = MAX(0.0, p_unscaled_process_step);
	ui_time_elapsed += ui_unscaled_delta;
	narrative_delta = narrative_paused ? 0.0 : (ui_unscaled_delta * narrative_time_scale);
	narrative_time_elapsed += narrative_delta;

	// 3. Advance Simulation Clock with spiral-of-death frame budget guard.
	uint64_t sim_start_usec = OS::get_singleton() ? OS::get_singleton()->get_ticks_usec() : 0;
	int steps_run = 0;

	while (pending_manual_steps > 0 && steps_run < max_sim_steps_per_frame) {
		_execute_single_sim_step();
		pending_manual_steps--;
		steps_run++;
		if (OS::get_singleton() && (OS::get_singleton()->get_ticks_usec() - sim_start_usec) >= max_sim_frame_budget_usec) {
			break;
		}
	}

	if (!sim_paused && sim_time_scale > 0.0) {
		sim_accumulator += ui_unscaled_delta * sim_time_scale;
		while (sim_accumulator >= sim_fixed_step && steps_run < max_sim_steps_per_frame) {
			_execute_single_sim_step();
			sim_accumulator -= sim_fixed_step;
			steps_run++;
			if (OS::get_singleton() && (OS::get_singleton()->get_ticks_usec() - sim_start_usec) >= max_sim_frame_budget_usec) {
				// Clamp excess accumulator to avoid spiral-of-death starvation of physics & UI.
				sim_accumulator = MIN(sim_accumulator, sim_fixed_step);
				break;
			}
		}
		if (steps_run >= max_sim_steps_per_frame && sim_accumulator >= sim_fixed_step) {
			sim_accumulator = fmod(sim_accumulator, sim_fixed_step);
		}
	}

	last_sim_steps_executed = steps_run;
	if (OS::get_singleton()) {
		last_sim_frame_usec = OS::get_singleton()->get_ticks_usec() - sim_start_usec;
	}

	// 4. Drive decoupled narrative, hydration bubble, and streaming frame updates.
	if (narrative_update_callback) {
		narrative_update_callback(narrative_delta);
	}
	if (hydration_update_callback) {
		hydration_update_callback(ui_unscaled_delta);
	}
	if (streaming_update_callback) {
		streaming_update_callback(ui_unscaled_delta);
	}
}

void SandboxClock::reset() {
	sim_paused = false;
	sim_time_scale = 1.0;
	sim_fixed_step = 0.05;
	sim_accumulator = 0.0;
	sim_tick_index = 0;
	sim_time_elapsed = 0.0;
	pending_manual_steps = 0;
	last_sim_frame_usec = 0;
	last_sim_steps_executed = 0;

	physics_step_delta = 1.0 / 60.0;
	physics_time_elapsed = 0.0;
	physics_frame_count = 0;
	last_physics_steps_executed = 0;

	narrative_paused = false;
	narrative_time_scale = 1.0;
	narrative_delta = 0.0;
	narrative_time_elapsed = 0.0;
	ui_unscaled_delta = 0.0;
	ui_time_elapsed = 0.0;
}

void SandboxClock::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_sim_paused", "paused"), &SandboxClock::set_sim_paused);
	ClassDB::bind_method(D_METHOD("is_sim_paused"), &SandboxClock::is_sim_paused);
	ClassDB::bind_method(D_METHOD("set_sim_time_scale", "scale"), &SandboxClock::set_sim_time_scale);
	ClassDB::bind_method(D_METHOD("get_sim_time_scale"), &SandboxClock::get_sim_time_scale);
	ClassDB::bind_method(D_METHOD("set_sim_fixed_step", "step"), &SandboxClock::set_sim_fixed_step);
	ClassDB::bind_method(D_METHOD("get_sim_fixed_step"), &SandboxClock::get_sim_fixed_step);
	ClassDB::bind_method(D_METHOD("set_sim_tick_rate_hz", "hz"), &SandboxClock::set_sim_tick_rate_hz);
	ClassDB::bind_method(D_METHOD("get_sim_tick_rate_hz"), &SandboxClock::get_sim_tick_rate_hz);
	ClassDB::bind_method(D_METHOD("get_sim_tick_index"), &SandboxClock::get_sim_tick_index);
	ClassDB::bind_method(D_METHOD("get_sim_time_elapsed"), &SandboxClock::get_sim_time_elapsed);
	ClassDB::bind_method(D_METHOD("step", "steps"), &SandboxClock::step, DEFVAL(1));
	ClassDB::bind_method(D_METHOD("step_immediate", "steps"), &SandboxClock::step_immediate, DEFVAL(1));
	ClassDB::bind_method(D_METHOD("set_max_sim_steps_per_frame", "max_steps"), &SandboxClock::set_max_sim_steps_per_frame);
	ClassDB::bind_method(D_METHOD("get_max_sim_steps_per_frame"), &SandboxClock::get_max_sim_steps_per_frame);
	ClassDB::bind_method(D_METHOD("set_max_sim_frame_budget_usec", "budget_usec"), &SandboxClock::set_max_sim_frame_budget_usec);
	ClassDB::bind_method(D_METHOD("get_max_sim_frame_budget_usec"), &SandboxClock::get_max_sim_frame_budget_usec);
	ClassDB::bind_method(D_METHOD("get_last_sim_frame_usec"), &SandboxClock::get_last_sim_frame_usec);
	ClassDB::bind_method(D_METHOD("get_last_sim_steps_executed"), &SandboxClock::get_last_sim_steps_executed);

	ClassDB::bind_method(D_METHOD("get_physics_step_delta"), &SandboxClock::get_physics_step_delta);
	ClassDB::bind_method(D_METHOD("get_physics_time_elapsed"), &SandboxClock::get_physics_time_elapsed);
	ClassDB::bind_method(D_METHOD("get_physics_frame_count"), &SandboxClock::get_physics_frame_count);
	ClassDB::bind_method(D_METHOD("get_last_physics_steps_executed"), &SandboxClock::get_last_physics_steps_executed);

	ClassDB::bind_method(D_METHOD("set_narrative_paused", "paused"), &SandboxClock::set_narrative_paused);
	ClassDB::bind_method(D_METHOD("is_narrative_paused"), &SandboxClock::is_narrative_paused);
	ClassDB::bind_method(D_METHOD("set_narrative_time_scale", "scale"), &SandboxClock::set_narrative_time_scale);
	ClassDB::bind_method(D_METHOD("get_narrative_time_scale"), &SandboxClock::get_narrative_time_scale);
	ClassDB::bind_method(D_METHOD("get_narrative_delta"), &SandboxClock::get_narrative_delta);
	ClassDB::bind_method(D_METHOD("get_narrative_time_elapsed"), &SandboxClock::get_narrative_time_elapsed);
	ClassDB::bind_method(D_METHOD("get_ui_unscaled_delta"), &SandboxClock::get_ui_unscaled_delta);
	ClassDB::bind_method(D_METHOD("get_ui_time_elapsed"), &SandboxClock::get_ui_time_elapsed);

	ClassDB::bind_method(D_METHOD("advance", "unscaled_process_step", "physics_step", "physics_steps"), &SandboxClock::advance);
	ClassDB::bind_method(D_METHOD("reset"), &SandboxClock::reset);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "sim_paused"), "set_sim_paused", "is_sim_paused");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sim_time_scale", PROPERTY_HINT_RANGE, "0.0,100.0,0.1"), "set_sim_time_scale", "get_sim_time_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sim_fixed_step", PROPERTY_HINT_RANGE, "0.001,1.0,0.001"), "set_sim_fixed_step", "get_sim_fixed_step");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "narrative_paused"), "set_narrative_paused", "is_narrative_paused");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "narrative_time_scale", PROPERTY_HINT_RANGE, "0.0,10.0,0.05"), "set_narrative_time_scale", "get_narrative_time_scale");
}

SandboxClock::SandboxClock() {
	if (!singleton) {
		singleton = this;
	}
}

SandboxClock::~SandboxClock() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
