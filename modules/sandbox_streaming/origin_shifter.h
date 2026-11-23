/**************************************************************************/
/*  origin_shifter.h                                                      */
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

#include "core/object/object.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/variant/variant.h"

class Node3D;

class OriginShifter : public Object {
	GDCLASS(OriginShifter, Object);

	static inline OriginShifter *singleton = nullptr;
	double scene_origin[3] = {};
	real_t shift_threshold = 2048;
	HashSet<ObjectID> roots;
	HashSet<ObjectID> invokers;

	struct ProxyBuffer {
		PackedFloat32Array data;
		int stride = 12;
	};
	HashMap<RID, ProxyBuffer> multimeshes;

	void _translate(const Vector3 &p_delta, Node3D *p_primary);

protected:
	static void _bind_methods();

public:
	static OriginShifter *get_singleton() { return singleton; }
	PackedFloat64Array get_origin_64() const;
	void set_shift_threshold(real_t p_threshold);
	real_t get_shift_threshold() const { return shift_threshold; }

	void register_root(Node3D *p_root);
	void unregister_root(Node3D *p_root);
	void register_invoker(Node3D *p_invoker);
	void unregister_invoker(Node3D *p_invoker);

	// Submit identity-rooted 3D proxy batches here so shifts can rebase the
	// current physics buffer, even while the GPU displays an interpolated frame.
	// The producer owns the RID and unregisters it before freeing it.
	void set_multimesh_buffer(RID p_multimesh, const PackedFloat32Array &p_buffer, int p_stride);
	void unregister_multimesh(RID p_multimesh);

	void sync_from_world_state();
	bool check_and_shift(Node3D *p_primary);

	OriginShifter();
	~OriginShifter();
};
