/**************************************************************************/
/*  sim_module.h                                                          */
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

#include "core/io/resource.h"
#include "core/object/class_db.h"
#include "core/object/gdvirtual.gen.h"
#include "core/variant/dictionary.h"

class WorldState;
class EventBus;

class SimModule : public Resource {
	GDCLASS(SimModule, Resource);

private:
	StringName module_id;
	double tick_rate_hz = 2.0;
	int priority = 0;
	bool thread_safe_tick = false;

	void _on_init_bind(Object *p_state, Object *p_events);
	void _on_tick_bind(double p_delta_sim, Object *p_state, Object *p_events);

protected:
	static void _bind_methods();

	GDVIRTUAL2(_on_init, Object *, Object *);
	GDVIRTUAL3(_on_tick, double, Object *, Object *);
	GDVIRTUAL0RC(PackedByteArray, _on_snapshot_write);
	GDVIRTUAL1(_on_snapshot_read, PackedByteArray);
	GDVIRTUAL0RC(Dictionary, _get_declared_schema);

public:
	void set_module_id(const StringName &p_id) { module_id = p_id; }
	StringName get_module_id() const { return module_id; }

	void set_tick_rate_hz(double p_hz);
	double get_tick_rate_hz() const { return tick_rate_hz; }

	void set_priority(int p_priority) { priority = p_priority; }
	int get_priority() const { return priority; }

	void set_thread_safe_tick(bool p_thread_safe) { thread_safe_tick = p_thread_safe; }
	bool is_thread_safe_tick() const { return thread_safe_tick; }

	virtual void on_init(WorldState *p_state, EventBus *p_events);
	virtual void on_tick(double p_delta_sim, WorldState *p_state, EventBus *p_events);
	virtual PackedByteArray on_snapshot_write() const;
	virtual void on_snapshot_read(const PackedByteArray &p_data);
	virtual Dictionary get_declared_schema() const;

	SimModule() {}
	virtual ~SimModule() {}
};
