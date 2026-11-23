/**************************************************************************/
/*  hydratable_actor.h                                                    */
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

#include "scene/main/node.h"

class HydratableActor : public Node {
	GDCLASS(HydratableActor, Node);

	StringName archetype_id;
	real_t hydrate_radius = 140.0;
	real_t dehydrate_radius = 165.0;
	RID bound_rid;
	Dictionary state;

protected:
	static void _bind_methods();

public:
	void set_archetype_id(const StringName &p_archetype) { archetype_id = p_archetype; }
	StringName get_archetype_id() const { return archetype_id; }
	void set_hydrate_radius(real_t p_radius);
	real_t get_hydrate_radius() const { return hydrate_radius; }
	void set_dehydrate_radius(real_t p_radius);
	real_t get_dehydrate_radius() const { return dehydrate_radius; }

	RID get_bound_rid() const { return bound_rid; }
	Dictionary get_state() const { return state; }
	void set_state(const Dictionary &p_state);
	void set_pinned(bool p_pinned, const StringName &p_reason);
	static uint32_t get_pin_flag(const StringName &p_reason);

	void hydrate(RID p_rid, const Dictionary &p_initial_state);
	Dictionary capture_state();
	Dictionary dehydrate();
};
