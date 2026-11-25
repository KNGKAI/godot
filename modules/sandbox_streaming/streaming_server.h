/**************************************************************************/
/*  streaming_server.h                                                    */
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

#include "cell_residency.h"
#include "world_partition_manifest.h"

#include "core/object/object.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

class StreamingInvoker;

class StreamingServer : public Object {
	GDCLASS(StreamingServer, Object);

	struct Layer {
		double size = 0;
		double load_radius = 0;
		double evict_radius = 0;
		HashMap<Vector2i, int> grid;
	};
	struct Cell {
		StringName id;
		StringName layer;
		Vector2i coords;
		CellResidency::Paths paths;
	};
	struct Demand {
		double distance = 0;
		double priority = 0;
		CellResidency::Lod lod = CellResidency::LOD_NONE;
	};
	struct Candidate {
		int index = 0;
		Demand demand;
		bool operator<(const Candidate &p_other) const {
			return demand.priority == p_other.demand.priority ? index < p_other.index : demand.priority > p_other.demand.priority;
		}
	};

	static inline StreamingServer *singleton = nullptr;
	LocalVector<Cell> cells;
	HashMap<StringName, Layer> layers;
	HashMap<int, CellResidency *> residents;
	HashSet<int> pins;
	LocalVector<ObjectID> invokers;
	ObjectID stream_root;
	ObjectID scene_id;
	bool enabled = false;
	bool updating = false;
	int forced_hlod = -1;
	uint64_t budget_usec = 2000;
	uint64_t last_activation_usec = 0;
	int queued_count = 0;

	Node3D *_get_stream_root();
	void _clear_residency();
	void _evaluate(StreamingInvoker *p_invoker, const PackedFloat64Array &p_origin, HashMap<int, Demand> &r_wanted) const;
	CellResidency::Lod _select_lod(const Cell &p_cell, double p_distance, double p_radius) const;
	static void _frame(double p_delta);
	static Error _restore_diffs(const Dictionary &p_diffs);
	static void _physics_start(double p_delta);
	Variant _cmd_pin(const StringName &p_layer, int p_x, int p_z);
	Variant _cmd_unpin(const StringName &p_layer, int p_x, int p_z);
	Variant _cmd_budget(double p_ms);
	Variant _cmd_force(const String &p_mode);

protected:
	static void _bind_methods();

public:
	static StreamingServer *get_singleton() { return singleton; }
	void set_enabled(bool p_enabled);
	bool is_enabled() const { return enabled; }
	Error set_manifest(const Ref<WorldPartitionManifest> &p_manifest);
	Error set_pinned(const StringName &p_layer, const Vector2i &p_coords, bool p_pinned);
	Error set_budget_ms(double p_ms);
	Error set_hlod_force(const String &p_mode);
	void register_invoker(StreamingInvoker *p_invoker);
	void unregister_invoker(StreamingInvoker *p_invoker);
	const LocalVector<ObjectID> &get_invokers() const { return invokers; }
	void update(double p_delta);
	Dictionary get_stats() const;
#ifdef DEBUG_ENABLED
	Dictionary get_debug_cells() const;
#endif
	StreamingServer();
	~StreamingServer();
};
