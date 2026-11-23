/**************************************************************************/
/*  hydration_proxy_batch.cpp                                             */
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

#include "hydration_proxy_batch.h"

#include "core/os/thread.h"
#include "servers/rendering/rendering_server.h"

#include "modules/sandbox_streaming/origin_shifter.h"

void HydrationProxyBatch::_release() {
	if (multimesh.is_valid()) {
		if (OriginShifter *shifter = OriginShifter::get_singleton()) {
			shifter->unregister_multimesh(multimesh);
		}
		if (RenderingServer *rs = RenderingServer::get_singleton()) {
			if (instance.is_valid()) {
				rs->free_rid(instance);
			}
			rs->free_rid(multimesh);
		}
	}
	instance = RID();
	multimesh = RID();
	buffers[0].clear();
	buffers[1].clear();
	count = 0;
	visible_count = 0;
	gpu_capacity = 0;
}

void HydrationProxyBatch::configure(const Ref<Mesh> &p_mesh, RID p_scenario) {
	ERR_FAIL_COND(!Thread::is_main_thread());
	if (mesh == p_mesh && scenario == p_scenario) {
		return;
	}
	_release();
	mesh = p_mesh;
	scenario = p_scenario;
}

void HydrationProxyBatch::begin() {
	ERR_FAIL_COND(!Thread::is_main_thread());
	write_buffer = 1 - write_buffer;
	count = 0;
}

void HydrationProxyBatch::append(const Transform3D &p_transform) {
	ERR_FAIL_COND(!Thread::is_main_thread());
	if (mesh.is_null() || scenario.is_null()) {
		return;
	}
	PackedFloat32Array &buffer = buffers[write_buffer];
	int64_t offset = int64_t(count) * TRANSFORM_STRIDE;
	if (offset == buffer.size()) {
		int64_t new_capacity = MAX(int64_t(gpu_capacity), MAX(int64_t(1), int64_t(count) * 2));
		ERR_FAIL_COND(buffer.resize_initialized(new_capacity * TRANSFORM_STRIDE) != OK);
	}
	float *data = buffer.ptrw() + offset;
	for (int row = 0; row < 3; row++) {
		const Vector3 &basis_row = p_transform.basis[row];
		data[4 * row] = basis_row.x;
		data[4 * row + 1] = basis_row.y;
		data[4 * row + 2] = basis_row.z;
		data[4 * row + 3] = p_transform.origin[row];
	}
	count++;
}

void HydrationProxyBatch::submit() {
	ERR_FAIL_COND(!Thread::is_main_thread());
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!rs || mesh.is_null() || scenario.is_null()) {
		return;
	}
	OriginShifter *shifter = OriginShifter::get_singleton();
	if (count == 0) {
		if (visible_count > 0) {
			rs->multimesh_set_visible_instances(multimesh, 0);
			if (shifter) {
				shifter->unregister_multimesh(multimesh);
			}
			visible_count = 0;
		}
		return;
	}

	if (multimesh.is_null()) {
		RID mesh_rid = mesh->get_rid();
		if (mesh_rid.is_null()) {
			return;
		}
		multimesh = rs->multimesh_create();
		rs->multimesh_set_mesh(multimesh, mesh_rid);
		instance = rs->instance_create2(multimesh, scenario);
	}

	PackedFloat32Array &buffer = buffers[write_buffer];
	int capacity = buffer.size() / TRANSFORM_STRIDE;
	if (capacity > gpu_capacity) {
		rs->multimesh_allocate_data(multimesh, capacity, RSE::MULTIMESH_TRANSFORM_3D);
		gpu_capacity = capacity;
	} else if (capacity < gpu_capacity) {
		// The other staging buffer grew on the previous submission.
		ERR_FAIL_COND(buffer.resize_initialized(int64_t(gpu_capacity) * TRANSFORM_STRIDE) != OK);
	}
	if (shifter) {
		shifter->set_multimesh_buffer(multimesh, buffer, TRANSFORM_STRIDE);
	} else {
		rs->multimesh_set_buffer(multimesh, buffer);
	}
	rs->multimesh_set_visible_instances(multimesh, count);
	visible_count = count;
}

HydrationProxyBatch::~HydrationProxyBatch() {
	_release();
}
