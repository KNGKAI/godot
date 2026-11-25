/**************************************************************************/
/*  world_state.h                                                         */
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
#include "core/object/class_db.h"
#include "core/object/object.h"
#include "core/os/mutex.h"
#include "core/os/rw_lock.h"
#include "core/templates/a_hash_map.h"
#include "core/templates/local_vector.h"
#include "core/templates/rid.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

#include <atomic>

class WorldState : public Object {
	GDCLASS(WorldState, Object);

public:
	enum ScalarColumnType : uint8_t {
		COLUMN_FLOAT = 0,
		COLUMN_INT = 1,
		COLUMN_BOOL = 2,
		COLUMN_STRING = 3,
		COLUMN_VEC3 = 4,
	};

	struct ScalarChannelMeta {
		StringName key;
		ScalarColumnType type = COLUMN_FLOAT;
		uint32_t dense_index = 0;
	};

	struct DeferredEntitySpawn {
		RID entity_rid;
		double pos_x = 0.0;
		double pos_y = 0.0;
		double pos_z = 0.0;
		Vector3 velocity;
		float health = 100.0f;
		uint32_t flags = 0;
	};

	struct DeferredScalarMutation {
		StringName key;
		Variant value;
	};

private:
	static inline WorldState *singleton = nullptr;

	mutable RWLock state_rw_lock;
	std::atomic<bool> structure_locked{ false };

	// 1. Named Scalar SoA Channel Bank (Finding 3)
	AHashMap<StringName, uint32_t> scalar_key_to_meta;
	LocalVector<ScalarChannelMeta> scalar_meta;

	LocalVector<double> float_column;
	LocalVector<int64_t> int_column;
	LocalVector<uint8_t> bool_column;
	LocalVector<String> string_column;
	LocalVector<Vector3> vec3_column;

	// 2. Contiguous RID-Indexed Multi-Column Entity SoA Table (Finding 3 & Invariant #3)
	AHashMap<uint64_t, uint32_t> entity_rid_to_row;
	LocalVector<RID> entity_rids;
	LocalVector<double> entity_pos_x;
	LocalVector<double> entity_pos_y;
	LocalVector<double> entity_pos_z;
	LocalVector<Vector3> entity_vel;
	LocalVector<float> entity_health;
	LocalVector<uint32_t> entity_flags;

	// Pending structural mutations queued during parallel WorkerThreadPool execution
	Mutex deferred_structure_mutex;
	LocalVector<DeferredScalarMutation> pending_scalar_mutations;
	LocalVector<DeferredEntitySpawn> pending_entity_spawns;
	LocalVector<RID> pending_entity_removals;

	// 64-bit Authoritative World Origin (Schema Envelope v2 & OriginShifter)
	double world_origin_64[3] = { 0.0, 0.0, 0.0 };

	int32_t _allocate_entity_row_unlocked(RID p_rid, double p_x, double p_y, double p_z, const Vector3 &p_vel, float p_health, uint32_t p_flags);
	bool _remove_entity_row_unlocked(RID p_rid);

protected:
	static void _bind_methods();

public:
	static WorldState *get_singleton() { return singleton; }

	// Concurrency Invariant #3: Structural Lock & Single-Threaded Commit
	void lock_structure();
	void unlock_and_commit_structural_changes();
	bool is_structure_locked() const { return structure_locked.load(std::memory_order_acquire); }

	// Named Scalar SoA Channel API
	int32_t register_float_channel(const StringName &p_key, double p_initial = 0.0);
	int32_t register_int_channel(const StringName &p_key, int64_t p_initial = 0);
	int32_t register_bool_channel(const StringName &p_key, bool p_initial = false);
	int32_t register_string_channel(const StringName &p_key, const String &p_initial = String());
	int32_t register_vec3_channel(const StringName &p_key, const Vector3 &p_initial = Vector3());

	void set_float(const StringName &p_key, double p_value);
	double get_float(const StringName &p_key, double p_default = 0.0) const;

	void set_int(const StringName &p_key, int64_t p_value);
	int64_t get_int(const StringName &p_key, int64_t p_default = 0) const;

	void set_bool(const StringName &p_key, bool p_value);
	bool get_bool(const StringName &p_key, bool p_default = false) const;

	void set_string(const StringName &p_key, const String &p_value);
	String get_string(const StringName &p_key, const String &p_default = String()) const;

	void set_vec3(const StringName &p_key, const Vector3 &p_value);
	Vector3 get_vec3(const StringName &p_key, const Vector3 &p_default = Vector3()) const;

	void set_value(const StringName &p_key, const Variant &p_value);
	Variant get_value(const StringName &p_key, const Variant &p_default = Variant()) const;
	void interpolate_value(const StringName &p_key, const Variant &p_from, const Variant &p_to, double p_weight);

	bool has_key(const StringName &p_key) const;
	PackedStringArray get_all_keys() const;
	uint32_t get_scalar_channel_count() const;

	// Fast raw pointer access for C++ WorkerThreadPool tasks (valid while structure_locked == true)
	double *get_float_column_ptr() { return float_column.ptr(); }
	const double *get_float_column_ptr() const { return float_column.ptr(); }
	int64_t *get_int_column_ptr() { return int_column.ptr(); }
	const int64_t *get_int_column_ptr() const { return int_column.ptr(); }

	// Entity SoA Table API
	int32_t allocate_entity_row(RID p_rid, double p_x = 0.0, double p_y = 0.0, double p_z = 0.0, const Vector3 &p_vel = Vector3(), float p_health = 100.0f, uint32_t p_flags = 0);
	bool remove_entity_row(RID p_rid);
	void queue_entity_spawn(RID p_rid, double p_x, double p_y, double p_z, const Vector3 &p_vel = Vector3(), float p_health = 100.0f, uint32_t p_flags = 0);
	void queue_entity_removal(RID p_rid);

	int32_t get_entity_row_index(RID p_rid) const;
	uint32_t get_entity_count() const { return entity_rids.size(); }

	double *get_entity_pos_x_ptr() { return entity_pos_x.ptr(); }
	double *get_entity_pos_y_ptr() { return entity_pos_y.ptr(); }
	double *get_entity_pos_z_ptr() { return entity_pos_z.ptr(); }
	Vector3 *get_entity_vel_ptr() { return entity_vel.ptr(); }
	float *get_entity_health_ptr() { return entity_health.ptr(); }
	uint32_t *get_entity_flags_ptr() { return entity_flags.ptr(); }

	void set_entity_position_64(RID p_rid, double p_x, double p_y, double p_z);
	Vector3 get_entity_position_local(RID p_rid) const;
	void set_entity_velocity(RID p_rid, const Vector3 &p_vel);
	Vector3 get_entity_velocity(RID p_rid) const;
	void set_entity_health(RID p_rid, float p_health);
	float get_entity_health(RID p_rid) const;

	// 64-bit World Origin API
	void set_world_origin_64(double p_x, double p_y, double p_z);
	void get_world_origin_64(double &r_x, double &r_y, double &r_z) const;
	void set_world_origin_64_array(const Array &p_origin);
	Array get_world_origin_64_array() const;

	// Serialization Snapshot Helpers
	Dictionary to_dictionary() const;
	Error from_dictionary(const Dictionary &p_dict);
	void clear();

	WorldState();
	~WorldState();
};
