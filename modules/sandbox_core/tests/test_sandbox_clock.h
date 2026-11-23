/**************************************************************************/
/*  test_sandbox_clock.h                                                  */
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

#include "../event_bus.h"
#include "../sandbox_clock.h"
#include "../sandbox_schema_registry.h"
#include "../world_state.h"

#include "servers/audio/audio_server.h"
#include "tests/test_macros.h"

namespace TestSandboxClock {

TEST_CASE("[SandboxCore][Gate1] Multi-Clock Isolation Test (50x Fast-Forward & 0x Paused)") {
	SandboxClock *registered_clock = SandboxClock::get_singleton();
	REQUIRE(registered_clock != nullptr);
	SandboxClock *clock = memnew(SandboxClock);
	CHECK(SandboxClock::get_singleton() == registered_clock);
	clock->set_sim_fixed_step(0.05); // 20 Hz simulation tick
	clock->set_max_sim_steps_per_frame(128);

	const double frame_delta_60hz = 1.0 / 60.0; // ~16.666 ms

	// 1. Run sim_clock at 50x speed for 60 frames (1.0 real second -> 50.0 simulated seconds = 1000 sim ticks).
	clock->set_sim_time_scale(50.0);
	clock->set_sim_paused(false);

	for (int f = 0; f < 60; f++) {
		clock->notify_physics_step_start(frame_delta_60hz);
		clock->advance(frame_delta_60hz, frame_delta_60hz, 1);
	}

	CHECK(clock->get_physics_frame_count() == 60);
	CHECK(Math::is_equal_approx(clock->get_physics_step_delta(), frame_delta_60hz));
	CHECK(Math::is_equal_approx(clock->get_physics_time_elapsed(), 1.0, 0.001));
	CHECK(Math::is_equal_approx(clock->get_ui_unscaled_delta(), frame_delta_60hz));
	CHECK(clock->get_sim_tick_index() >= 999);
	CHECK(clock->get_sim_tick_index() <= 1000);

	if (AudioServer::get_singleton()) {
		CHECK(Math::is_equal_approx((double)AudioServer::get_singleton()->get_playback_speed_scale(), 1.0));
	}

	// 2. Pause sim_clock (0x): verify physics & UI clocks continue advancing while sim_clock stays frozen.
	uint64_t ticks_at_pause = clock->get_sim_tick_index();
	double sim_time_at_pause = clock->get_sim_time_elapsed();
	clock->set_sim_paused(true);

	for (int f = 0; f < 30; f++) {
		clock->notify_physics_step_start(frame_delta_60hz);
		clock->advance(frame_delta_60hz, frame_delta_60hz, 1);
	}

	CHECK(clock->get_sim_tick_index() == ticks_at_pause);
	CHECK(Math::is_equal_approx(clock->get_sim_time_elapsed(), sim_time_at_pause));
	CHECK(clock->get_physics_frame_count() == 90);
	CHECK(Math::is_equal_approx(clock->get_ui_time_elapsed(), 1.5, 0.001));

	// 3. Single-step sim_clock while paused via step(3).
	clock->step(3);
	clock->advance(frame_delta_60hz, frame_delta_60hz, 1);
	CHECK(clock->get_sim_tick_index() == ticks_at_pause + 3);

	memdelete(clock);
	CHECK(SandboxClock::get_singleton() == registered_clock);
}

TEST_CASE("[SandboxCore] WorldState Dual SoA Storage & Structural Lock Invariant #3") {
	WorldState *ws = WorldState::get_singleton();
	REQUIRE(ws != nullptr);
	ws->clear();

	ws->set_float(SNAME("weather.rain_intensity"), 0.42);
	ws->set_int(SNAME("power.grid_tier"), 3);
	ws->set_bool(SNAME("alarm.sector_a"), true);
	ws->set_string(SNAME("district.name"), "Downtown");

	CHECK(Math::is_equal_approx(ws->get_float(SNAME("weather.rain_intensity")), 0.42));
	CHECK(ws->get_int(SNAME("power.grid_tier")) == 3);
	CHECK(ws->get_bool(SNAME("alarm.sector_a")) == true);
	CHECK(ws->get_string(SNAME("district.name")) == "Downtown");

	// Timeline interpolation test
	ws->interpolate_value(SNAME("weather.rain_intensity"), 0.0, 1.0, 0.75);
	CHECK(Math::is_equal_approx(ws->get_float(SNAME("weather.rain_intensity")), 0.75));

	// Structural lock during WorkerThreadPool tick (Invariant #3)
	uint32_t count_before = ws->get_scalar_channel_count();
	ws->lock_structure();
	CHECK(ws->is_structure_locked() == true);

	// Updating an existing pre-allocated SoA channel in-place succeeds immediately:
	ws->set_float(SNAME("weather.rain_intensity"), 0.95);
	CHECK(Math::is_equal_approx(ws->get_float(SNAME("weather.rain_intensity")), 0.95));

	// Adding a new key while locked is deferred until unlock_and_commit_structural_changes():
	ws->set_float(SNAME("deferred.new_metric"), 12.5);
	CHECK(ws->get_scalar_channel_count() == count_before);

	ws->unlock_and_commit_structural_changes();
	CHECK(ws->is_structure_locked() == false);
	CHECK(ws->get_scalar_channel_count() == count_before + 1);
	CHECK(Math::is_equal_approx(ws->get_float(SNAME("deferred.new_metric")), 12.5));

	ws->clear();
}

TEST_CASE("[SandboxCore] EventBus Deferred Dispatch & Schema Registry Validation") {
	EventBus *eb = EventBus::get_singleton();
	REQUIRE(eb != nullptr);
	eb->clear_all();

	Dictionary payload;
	payload["sector"] = "A";
	eb->dispatch_deferred(SNAME("power_outage"), payload);
	CHECK(eb->get_pending_deferred_count() == 1);

	eb->flush_deferred();
	CHECK(eb->get_pending_deferred_count() == 0);

	TypedArray<Dictionary> hist = eb->get_history();
	REQUIRE(hist.size() == 1);
	Dictionary first = hist[0];
	CHECK(StringName(first["event_id"]) == SNAME("power_outage"));

	// Schema Registry validation checks
	Ref<SandboxSchemaRegistry> reg = SandboxSchemaRegistry::get_active_registry();
	REQUIRE(reg.is_valid());
	reg->clear();

	reg->register_state_key(SNAME("power.sector_a_load"), Variant::FLOAT, 50.0, Vector2(0.0, 100.0), false, "Sector A load");
	reg->register_state_key(SNAME("legacy.old_key"), Variant::INT, 0, Vector2(), true, "Deprecated key");

	CHECK(reg->validate_state_key(SNAME("power.sector_a_load"), Variant::FLOAT) == SandboxSchemaRegistry::VALIDATION_OK);
	CHECK(reg->validate_state_key(SNAME("power.sector_a_load"), Variant::STRING) == SandboxSchemaRegistry::VALIDATION_TYPE_MISMATCH);
	CHECK(reg->validate_state_key(SNAME("legacy.old_key")) == SandboxSchemaRegistry::VALIDATION_DEPRECATED);
	CHECK(reg->validate_state_key(SNAME("unknown.typo_key")) == SandboxSchemaRegistry::VALIDATION_MISSING_KEY);

	reg->clear();
	eb->clear_all();
}

} // namespace TestSandboxClock
