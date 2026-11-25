/**************************************************************************/
/*  streaming_invoker.cpp                                                 */
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

#include "streaming_invoker.h"

#include "origin_shifter.h"
#include "streaming_server.h"

#include "core/config/engine.h"
#include "modules/sandbox_core/world_state.h"

void StreamingInvoker::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			velocity = Vector3();
			sampled = false;
			hydration_sample_frame = UINT64_MAX;
			if (OriginShifter::get_singleton()) {
				OriginShifter::get_singleton()->register_invoker(this);
			}
			if (StreamingServer::get_singleton()) {
				StreamingServer::get_singleton()->register_invoker(this);
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (StreamingServer::get_singleton()) {
				StreamingServer::get_singleton()->unregister_invoker(this);
			}
			if (OriginShifter::get_singleton()) {
				OriginShifter::get_singleton()->unregister_invoker(this);
			}
		} break;
	}
}

void StreamingInvoker::sample(double p_delta) {
	if (!is_inside_tree() || !Math::is_finite(p_delta) || p_delta <= 0.0) {
		return;
	}
	const Vector3 position = get_global_position();
	double origin[3] = {};
#ifndef REAL_T_IS_DOUBLE
	if (WorldState *state = WorldState::get_singleton()) {
		state->get_world_origin_64(origin[0], origin[1], origin[2]);
	}
#endif
	if (sampled) {
		Vector3 instantaneous;
		for (int i = 0; i < 3; i++) {
			const double movement = (origin[i] - previous_origin[i]) + (double(position[i]) - previous_position[i]);
			instantaneous[i] = movement / p_delta;
		}
		// Exponential smoothing gives the same response at different frame rates.
		velocity = velocity.lerp(instantaneous, 1.0 - Math::exp(-p_delta / 0.25));
	}
	sampled = true;
	previous_position = position;
	for (int i = 0; i < 3; i++) {
		previous_origin[i] = origin[i];
	}
}

void StreamingInvoker::set_lookahead(double p_seconds) {
	ERR_FAIL_COND(!Math::is_finite(p_seconds) || p_seconds < 0.0);
	lookahead = p_seconds;
}

void StreamingInvoker::sample_for_hydration(double p_delta) {
	sample(p_delta);
	hydration_sample_frame = Engine::get_singleton()->get_process_frames();
}

void StreamingInvoker::sample_for_streaming(double p_delta) {
	// The clock updates hydration first. Both consumers need the same velocity,
	// including when the streaming loader has no manifest or work this frame.
	if (hydration_sample_frame != Engine::get_singleton()->get_process_frames()) {
		sample(p_delta);
	}
	hydration_sample_frame = UINT64_MAX;
}

void StreamingInvoker::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_velocity"), &StreamingInvoker::get_velocity);
	ClassDB::bind_method(D_METHOD("set_lookahead", "seconds"), &StreamingInvoker::set_lookahead);
	ClassDB::bind_method(D_METHOD("get_lookahead"), &StreamingInvoker::get_lookahead);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lookahead", PROPERTY_HINT_RANGE, "0,4,0.1,or_greater,suffix:s"), "set_lookahead", "get_lookahead");
}
