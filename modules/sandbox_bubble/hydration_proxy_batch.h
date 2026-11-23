/**************************************************************************/
/*  hydration_proxy_batch.h                                               */
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

#include "scene/resources/mesh.h"

// Main-thread owner of one identity-rooted rendering instance per archetype.
class HydrationProxyBatch {
	static constexpr int TRANSFORM_STRIDE = 12;

	Ref<Mesh> mesh;
	RID scenario;
	RID multimesh;
	RID instance;

	// OriginShifter retains the last submitted buffer. Alternate staging buffers
	// so writing the next frame does not copy the one still in use.
	PackedFloat32Array buffers[2];
	int write_buffer = 0;
	int count = 0;
	int gpu_capacity = 0;
	int visible_count = 0;

	void _release();

public:
	void configure(const Ref<Mesh> &p_mesh, RID p_scenario);
	void begin();
	void append(const Transform3D &p_transform);
	void submit();

	HydrationProxyBatch() = default;
	HydrationProxyBatch(const HydrationProxyBatch &) = delete;
	HydrationProxyBatch &operator=(const HydrationProxyBatch &) = delete;
	~HydrationProxyBatch();
};
