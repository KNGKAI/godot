/**************************************************************************/
/*  cell_residency.h                                                      */
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

#include "core/math/vector3.h"
#include "core/variant/typed_array.h"
#include "scene/resources/packed_scene.h"

class Node3D;

// Each entry is an independently instantiable subtree. Splitting a dense cell
// here preserves PackedScene ownership, node paths, signals, and _ready order.
class CellSceneBundle : public Resource {
	GDCLASS(CellSceneBundle, Resource);

	TypedArray<PackedScene> batches;

protected:
	static void _bind_methods();

public:
	void set_batches(const TypedArray<PackedScene> &p_batches) { batches = p_batches; }
	TypedArray<PackedScene> get_batches() const { return batches; }
};

class CellResidency {
public:
	enum Lod {
		LOD_NONE,
		LOD_PROXY,
		LOD_MID,
		LOD_HIGH,
	};
	enum State {
		UNLOADED,
		LOADING_ASYNC,
		STAGED,
		ACTIVE_ATTACHED,
		EVICTING,
	};

	struct Paths {
		String proxy;
		String mid;
		String high;
		String collision;
	};

	// PackedScene instantiation and SceneTree entry cannot yield. The server
	// shares one start slot among all cells for the frame. A standalone tick
	// can instead drain a cell up to its caller's deadline.
	struct SceneStep {
		uint64_t start_deadline_usec;
		bool taken = false;
		bool try_begin();
	};

private:
	struct Asset {
		String request; // One successful request owns exactly one load_threaded_get.
		Ref<Resource> resource;
		ObjectID staged_batch;
		int next_batch = 0;

		Error start(const String &p_path);
		Error poll();
	};

	struct Visual {
		ObjectID root;
		Vector<ObjectID> geometry;
	};

	struct ScanCursor {
		ObjectID node;
		int next_child = -1;
	};

	State state = UNLOADED;
	Lod active_lod = LOD_NONE;
	Lod pending_lod = LOD_NONE;
	Lod failed_lod = LOD_NONE;
	Error last_error = OK;
	ObjectID root;
	ObjectID collision;
	ObjectID pending_collision;
	Visual current;
	Visual pending;
	Visual outgoing;
	Asset visual_asset;
	Asset collision_asset;
	Vector<String> abandoned_requests;
	Vector<ScanCursor> scan_stack;
	Vector<ObjectID> hidden_batches;
	int reveal_index = 0;
	double fade_elapsed = 0.0;
	float fade_sample = 0.0f;
	int fade_index = 0;

	void discard_pending();
	void retire_anchor();
	void drain_requests(bool p_wait);
	void fail(Error p_error);
	Error attach_batches(Asset &r_asset, ObjectID &r_group, bool p_visual, uint64_t p_deadline_usec, SceneStep *p_step);
	void scan_geometry(uint64_t p_deadline_usec);
	void advance_fade(double p_delta, uint64_t p_deadline_usec);

public:
	CellResidency() = default;
	CellResidency(const CellResidency &) = delete;
	CellResidency &operator=(const CellResidency &) = delete;
	~CellResidency();

	// Main thread. Returns true once per successfully committed representation.
	bool tick(Lod p_target, const Paths &p_paths, Node3D *p_parent, const Vector3 &p_local_position, double p_delta, uint64_t p_deadline_usec, SceneStep *p_step = nullptr);
	void clear();
	State get_state() const { return state; }
	Lod get_active_lod() const { return active_lod; }
	Node3D *get_root() const;
	bool has_pending_request() const { return !visual_asset.request.is_empty() || !collision_asset.request.is_empty() || !abandoned_requests.is_empty(); }
	Error get_last_error() const { return last_error; }
};
