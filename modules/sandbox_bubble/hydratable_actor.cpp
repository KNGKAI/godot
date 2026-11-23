/**************************************************************************/
/*  hydratable_actor.cpp                                                  */
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

#include "hydratable_actor.h"

#include "core/os/thread.h"

#include "modules/sandbox_sim/simulation_server.h"

void HydratableActor::set_hydrate_radius(real_t p_radius) {
	ERR_FAIL_COND(!Math::is_finite(p_radius) || p_radius <= 0);
	hydrate_radius = p_radius;
}

void HydratableActor::set_dehydrate_radius(real_t p_radius) {
	ERR_FAIL_COND(!Math::is_finite(p_radius) || p_radius <= 0);
	dehydrate_radius = p_radius;
}

void HydratableActor::set_state(const Dictionary &p_state) {
	ERR_FAIL_COND(!Thread::is_main_thread());
	state = p_state.duplicate(true);
}

uint32_t HydratableActor::get_pin_flag(const StringName &p_reason) {
	if (p_reason == SNAME("mission")) {
		return SimulationServer::PIN_MISSION;
	}
	if (p_reason == SNAME("sequence")) {
		return SimulationServer::PIN_SEQUENCE;
	}
	if (p_reason == SNAME("player_occupied")) {
		return SimulationServer::PIN_PLAYER_OCCUPIED;
	}
	if (p_reason == SNAME("combat_lock")) {
		return SimulationServer::PIN_COMBAT_LOCK;
	}
	if (p_reason == SNAME("debug")) {
		return SimulationServer::PIN_DEBUG_COMMAND;
	}
	return SimulationServer::PIN_NONE;
}

void HydratableActor::set_pinned(bool p_pinned, const StringName &p_reason) {
	ERR_FAIL_COND(!Thread::is_main_thread());
	SimulationServer *server = SimulationServer::get_singleton();
	if (!server || !server->has_agent(bound_rid)) {
		return;
	}
	const uint32_t flag = get_pin_flag(p_reason);
	ERR_FAIL_COND_MSG(!flag, "Unknown hydration pin reason.");
	const uint32_t mask = server->get_agent_pin_mask(bound_rid);
	server->set_agent_pin_mask(bound_rid, p_pinned ? mask | flag : mask & ~flag);
}

void HydratableActor::hydrate(RID p_rid, const Dictionary &p_initial_state) {
	ERR_FAIL_COND(!Thread::is_main_thread() || !p_rid.is_valid());
	bound_rid = p_rid;
	set_state(p_initial_state);
	emit_signal(SNAME("hydrated"), bound_rid, state);
}

Dictionary HydratableActor::capture_state() {
	ERR_FAIL_COND_V(!Thread::is_main_thread() || !bound_rid.is_valid(), Dictionary());
	// The same signal lets a controller contribute its current values to both saves and exits.
	Dictionary snapshot = state.duplicate(true);
	emit_signal(SNAME("capturing_state"), bound_rid, snapshot);
	return snapshot;
}

Dictionary HydratableActor::dehydrate() {
	ERR_FAIL_COND_V(!Thread::is_main_thread() || !bound_rid.is_valid(), Dictionary());
	Dictionary snapshot = capture_state();
	emit_signal(SNAME("dehydrating"), bound_rid, snapshot);
	bound_rid = RID();
	state = Dictionary();
	return snapshot;
}

void HydratableActor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_archetype_id", "archetype_id"), &HydratableActor::set_archetype_id);
	ClassDB::bind_method(D_METHOD("get_archetype_id"), &HydratableActor::get_archetype_id);
	ClassDB::bind_method(D_METHOD("set_hydrate_radius", "radius"), &HydratableActor::set_hydrate_radius);
	ClassDB::bind_method(D_METHOD("get_hydrate_radius"), &HydratableActor::get_hydrate_radius);
	ClassDB::bind_method(D_METHOD("set_dehydrate_radius", "radius"), &HydratableActor::set_dehydrate_radius);
	ClassDB::bind_method(D_METHOD("get_dehydrate_radius"), &HydratableActor::get_dehydrate_radius);
	ClassDB::bind_method(D_METHOD("get_bound_rid"), &HydratableActor::get_bound_rid);
	ClassDB::bind_method(D_METHOD("get_state"), &HydratableActor::get_state);
	ClassDB::bind_method(D_METHOD("set_state", "state"), &HydratableActor::set_state);
	ClassDB::bind_method(D_METHOD("set_pinned", "pinned", "reason"), &HydratableActor::set_pinned);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "archetype_id"), "set_archetype_id", "get_archetype_id");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hydrate_radius", PROPERTY_HINT_RANGE, "0.01,1000,0.01,or_greater,suffix:m"), "set_hydrate_radius", "get_hydrate_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dehydrate_radius", PROPERTY_HINT_RANGE, "0.01,1000,0.01,or_greater,suffix:m"), "set_dehydrate_radius", "get_dehydrate_radius");
	ADD_PROPERTY(PropertyInfo(Variant::RID, "bound_rid", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_bound_rid");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "state", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_state", "get_state");

	ADD_SIGNAL(MethodInfo("hydrated", PropertyInfo(Variant::RID, "entity_rid"), PropertyInfo(Variant::DICTIONARY, "initial_state")));
	ADD_SIGNAL(MethodInfo("capturing_state", PropertyInfo(Variant::RID, "entity_rid"), PropertyInfo(Variant::DICTIONARY, "out_state")));
	ADD_SIGNAL(MethodInfo("dehydrating", PropertyInfo(Variant::RID, "entity_rid"), PropertyInfo(Variant::DICTIONARY, "out_state")));
}
