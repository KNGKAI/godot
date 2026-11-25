/**************************************************************************/
/*  test_origin_shifter.h                                                 */
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

#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "tests/test_macros.h"

#include "modules/sandbox_core/world_state.h"
#include "modules/sandbox_streaming/origin_shifter.h"
#include "modules/sandbox_streaming/streaming_invoker.h"

namespace TestOriginShifter {

class InterpolationProbe : public Node3D {
protected:
	void fti_pump_xform() override {
		Node3D::fti_pump_xform();
		last_reset_position = get_global_position();
		reset_count++;
	}

public:
	Vector3 last_reset_position;
	int reset_count = 0;
};

TEST_CASE("[SceneTree][SandboxStreaming][Gate6] Invoker keeps its velocity and world position over repeated origin shifts") {
	OriginShifter *shifter = OriginShifter::get_singleton();
	WorldState *state = WorldState::get_singleton();
	REQUIRE(shifter);
	REQUIRE(state);
	const Array old_origin = state->get_world_origin_64_array();
	const real_t old_threshold = shifter->get_shift_threshold();
	state->set_world_origin_64(0, 0, 0);
	shifter->sync_from_world_state();
	shifter->set_shift_threshold(2048);

	SceneTree *tree = SceneTree::get_singleton();
	const bool old_interpolation = tree->is_physics_interpolation_enabled();
	tree->set_physics_interpolation_enabled(true);
	InterpolationProbe *rig = memnew(InterpolationProbe);
	StreamingInvoker *invoker = memnew(StreamingInvoker);
	Node3D *independent = memnew(Node3D);
	rig->add_child(invoker);
	rig->add_child(independent);
	tree->get_root()->add_child(rig);
	independent->set_as_top_level(true);
	independent->set_global_position(Vector3(70, 20, 10));
	shifter->register_root(rig);
	shifter->register_root(invoker);

	const double tick = 1.0 / 60.0;
	invoker->sample(tick);
	int shifts = 0;
	// 90 m/s over 16.2 km, including boundary crossings inside a tick.
	for (int i = 0; i < 10800; i++) {
		rig->set_position(rig->get_position() + Vector3(1.5, 0, 0));
		int resets_before = rig->reset_count;
		const bool shifted = shifter->check_and_shift(invoker);
		invoker->sample(tick);
		if (shifted) {
			shifts++;
			CHECK(rig->reset_count > resets_before);
			CHECK(rig->last_reset_position == rig->get_global_position());
			CHECK(rig->get_global_position().length() <= 256);
			CHECK(invoker->get_position() == Vector3());
			CHECK(invoker->get_velocity().is_equal_approx(Vector3(90, 0, 0)));
		}
	}
	PackedFloat64Array origin = shifter->get_origin_64();
#ifdef REAL_T_IS_DOUBLE
	CHECK(shifts == 0);
	CHECK(origin[0] == 0);
#else
	CHECK(shifts >= 4);
	CHECK(origin[0] == Math::snapped(origin[0], 512.0));
#endif
	CHECK(origin[0] + invoker->get_global_position().x == 16200.0);
	CHECK(origin[0] + independent->get_global_position().x == 70.0);

#ifndef REAL_T_IS_DOUBLE
	// Restoring a snapshot with an earlier origin keeps live roots and
	// future velocity samples in the restored coordinate system.
	state->set_world_origin_64(1024, 0, 0);
	shifter->sync_from_world_state();
	CHECK(1024 + invoker->get_global_position().x == 16200.0);
	CHECK(1024 + independent->get_global_position().x == 70.0);
	invoker->sample(tick);
	CHECK(invoker->get_velocity().x > 0);
	CHECK(invoker->get_velocity().x < 90);
#endif

	shifter->unregister_root(invoker);
	shifter->unregister_root(rig);
	memdelete(rig);
	state->set_world_origin_64_array(old_origin);
#ifndef REAL_T_IS_DOUBLE
	shifter->sync_from_world_state();
#endif
	shifter->set_shift_threshold(old_threshold);
	tree->set_physics_interpolation_enabled(old_interpolation);
}

} // namespace TestOriginShifter
