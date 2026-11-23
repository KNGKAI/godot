/**************************************************************************/
/*  sim_module.cpp                                                        */
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

#include "sim_module.h"

#include "modules/sandbox_core/event_bus.h"
#include "modules/sandbox_core/world_state.h"

void SimModule::set_tick_rate_hz(double p_hz) {
	ERR_FAIL_COND(p_hz <= 0.0);
	tick_rate_hz = p_hz;
}

void SimModule::on_init(WorldState *p_state, EventBus *p_events) {
	GDVIRTUAL_CALL(_on_init, p_state, p_events);
}

void SimModule::on_tick(double p_delta_sim, WorldState *p_state, EventBus *p_events) {
	GDVIRTUAL_CALL(_on_tick, p_delta_sim, p_state, p_events);
}

void SimModule::_on_init_bind(Object *p_state, Object *p_events) {
	WorldState *state = Object::cast_to<WorldState>(p_state);
	EventBus *events = Object::cast_to<EventBus>(p_events);
	ERR_FAIL_COND_MSG(p_state && !state, "Expected a WorldState for the state argument.");
	ERR_FAIL_COND_MSG(p_events && !events, "Expected an EventBus for the events argument.");
	on_init(state, events);
}

void SimModule::_on_tick_bind(double p_delta_sim, Object *p_state, Object *p_events) {
	WorldState *state = Object::cast_to<WorldState>(p_state);
	EventBus *events = Object::cast_to<EventBus>(p_events);
	ERR_FAIL_COND_MSG(p_state && !state, "Expected a WorldState for the state argument.");
	ERR_FAIL_COND_MSG(p_events && !events, "Expected an EventBus for the events argument.");
	on_tick(p_delta_sim, state, events);
}

PackedByteArray SimModule::on_snapshot_write() const {
	PackedByteArray ret;
	GDVIRTUAL_CALL(_on_snapshot_write, ret);
	return ret;
}

void SimModule::on_snapshot_read(const PackedByteArray &p_data) {
	GDVIRTUAL_CALL(_on_snapshot_read, p_data);
}

Dictionary SimModule::get_declared_schema() const {
	Dictionary ret;
	GDVIRTUAL_CALL(_get_declared_schema, ret);
	return ret;
}

void SimModule::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_module_id", "id"), &SimModule::set_module_id);
	ClassDB::bind_method(D_METHOD("get_module_id"), &SimModule::get_module_id);
	ClassDB::bind_method(D_METHOD("set_tick_rate_hz", "hz"), &SimModule::set_tick_rate_hz);
	ClassDB::bind_method(D_METHOD("get_tick_rate_hz"), &SimModule::get_tick_rate_hz);
	ClassDB::bind_method(D_METHOD("set_priority", "priority"), &SimModule::set_priority);
	ClassDB::bind_method(D_METHOD("get_priority"), &SimModule::get_priority);
	ClassDB::bind_method(D_METHOD("set_thread_safe_tick", "thread_safe"), &SimModule::set_thread_safe_tick);
	ClassDB::bind_method(D_METHOD("is_thread_safe_tick"), &SimModule::is_thread_safe_tick);

	ClassDB::bind_method(D_METHOD("on_init", "state", "events"), &SimModule::_on_init_bind);
	ClassDB::bind_method(D_METHOD("on_tick", "delta_sim", "state", "events"), &SimModule::_on_tick_bind);
	ClassDB::bind_method(D_METHOD("on_snapshot_write"), &SimModule::on_snapshot_write);
	ClassDB::bind_method(D_METHOD("on_snapshot_read", "data"), &SimModule::on_snapshot_read);
	ClassDB::bind_method(D_METHOD("get_declared_schema"), &SimModule::get_declared_schema);

	GDVIRTUAL_BIND(_on_init, "state", "events");
	GDVIRTUAL_BIND(_on_tick, "delta_sim", "state", "events");
	GDVIRTUAL_BIND(_on_snapshot_write);
	GDVIRTUAL_BIND(_on_snapshot_read, "data");
	GDVIRTUAL_BIND(_get_declared_schema);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "module_id"), "set_module_id", "get_module_id");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tick_rate_hz", PROPERTY_HINT_RANGE, "0.1,120.0,0.1"), "set_tick_rate_hz", "get_tick_rate_hz");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "priority"), "set_priority", "get_priority");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "thread_safe_tick"), "set_thread_safe_tick", "is_thread_safe_tick");
}
