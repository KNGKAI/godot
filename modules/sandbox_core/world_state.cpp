/**************************************************************************/
/*  world_state.cpp                                                       */
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

#include "world_state.h"

#include "core/math/math_funcs.h"

void WorldState::lock_structure() {
	structure_locked.store(true, std::memory_order_release);
}

void WorldState::unlock_and_commit_structural_changes() {
	structure_locked.store(false, std::memory_order_release);

	LocalVector<DeferredScalarMutation> scalars;
	LocalVector<DeferredEntitySpawn> spawns;
	LocalVector<RID> removals;
	{
		MutexLock lock(deferred_structure_mutex);
		scalars = std::move(pending_scalar_mutations);
		spawns = std::move(pending_entity_spawns);
		removals = std::move(pending_entity_removals);
		pending_scalar_mutations.clear();
		pending_entity_spawns.clear();
		pending_entity_removals.clear();
	}

	for (uint32_t i = 0; i < scalars.size(); i++) {
		set_value(scalars[i].key, scalars[i].value);
	}
	if (!spawns.is_empty() || !removals.is_empty()) {
		RWLockWrite write_lock(state_rw_lock);
		for (uint32_t i = 0; i < removals.size(); i++) {
			_remove_entity_row_unlocked(removals[i]);
		}
		for (uint32_t i = 0; i < spawns.size(); i++) {
			const DeferredEntitySpawn &s = spawns[i];
			_allocate_entity_row_unlocked(s.entity_rid, s.pos_x, s.pos_y, s.pos_z, s.velocity, s.health, s.flags);
		}
	}
}

int32_t WorldState::register_float_channel(const StringName &p_key, double p_initial) {
	ERR_FAIL_COND_V(p_key == StringName(), -1);
	ERR_FAIL_COND_V_MSG(is_structure_locked(), -1, "WorldState: Cannot resize SoA channels while structure_locked is true (Invariant #3).");
	RWLockWrite lock(state_rw_lock);
	const uint32_t *existing = scalar_key_to_meta.getptr(p_key);
	if (existing) {
		const ScalarChannelMeta &m = scalar_meta[*existing];
		ERR_FAIL_COND_V(m.type != COLUMN_FLOAT, -1);
		return (int32_t)m.dense_index;
	}
	uint32_t col_idx = float_column.size();
	float_column.push_back(p_initial);

	ScalarChannelMeta meta;
	meta.key = p_key;
	meta.type = COLUMN_FLOAT;
	meta.dense_index = col_idx;
	uint32_t meta_idx = scalar_meta.size();
	scalar_meta.push_back(meta);
	scalar_key_to_meta.insert(p_key, meta_idx);
	return (int32_t)col_idx;
}

int32_t WorldState::register_int_channel(const StringName &p_key, int64_t p_initial) {
	ERR_FAIL_COND_V(p_key == StringName(), -1);
	ERR_FAIL_COND_V_MSG(is_structure_locked(), -1, "WorldState: Cannot resize SoA channels while structure_locked is true (Invariant #3).");
	RWLockWrite lock(state_rw_lock);
	const uint32_t *existing = scalar_key_to_meta.getptr(p_key);
	if (existing) {
		const ScalarChannelMeta &m = scalar_meta[*existing];
		ERR_FAIL_COND_V(m.type != COLUMN_INT, -1);
		return (int32_t)m.dense_index;
	}
	uint32_t col_idx = int_column.size();
	int_column.push_back(p_initial);

	ScalarChannelMeta meta;
	meta.key = p_key;
	meta.type = COLUMN_INT;
	meta.dense_index = col_idx;
	uint32_t meta_idx = scalar_meta.size();
	scalar_meta.push_back(meta);
	scalar_key_to_meta.insert(p_key, meta_idx);
	return (int32_t)col_idx;
}

int32_t WorldState::register_bool_channel(const StringName &p_key, bool p_initial) {
	ERR_FAIL_COND_V(p_key == StringName(), -1);
	ERR_FAIL_COND_V_MSG(is_structure_locked(), -1, "WorldState: Cannot resize SoA channels while structure_locked is true (Invariant #3).");
	RWLockWrite lock(state_rw_lock);
	const uint32_t *existing = scalar_key_to_meta.getptr(p_key);
	if (existing) {
		const ScalarChannelMeta &m = scalar_meta[*existing];
		ERR_FAIL_COND_V(m.type != COLUMN_BOOL, -1);
		return (int32_t)m.dense_index;
	}
	uint32_t col_idx = bool_column.size();
	bool_column.push_back(p_initial ? 1 : 0);

	ScalarChannelMeta meta;
	meta.key = p_key;
	meta.type = COLUMN_BOOL;
	meta.dense_index = col_idx;
	uint32_t meta_idx = scalar_meta.size();
	scalar_meta.push_back(meta);
	scalar_key_to_meta.insert(p_key, meta_idx);
	return (int32_t)col_idx;
}

int32_t WorldState::register_string_channel(const StringName &p_key, const String &p_initial) {
	ERR_FAIL_COND_V(p_key == StringName(), -1);
	ERR_FAIL_COND_V_MSG(is_structure_locked(), -1, "WorldState: Cannot resize SoA channels while structure_locked is true (Invariant #3).");
	RWLockWrite lock(state_rw_lock);
	const uint32_t *existing = scalar_key_to_meta.getptr(p_key);
	if (existing) {
		const ScalarChannelMeta &m = scalar_meta[*existing];
		ERR_FAIL_COND_V(m.type != COLUMN_STRING, -1);
		return (int32_t)m.dense_index;
	}
	uint32_t col_idx = string_column.size();
	string_column.push_back(p_initial);

	ScalarChannelMeta meta;
	meta.key = p_key;
	meta.type = COLUMN_STRING;
	meta.dense_index = col_idx;
	uint32_t meta_idx = scalar_meta.size();
	scalar_meta.push_back(meta);
	scalar_key_to_meta.insert(p_key, meta_idx);
	return (int32_t)col_idx;
}

int32_t WorldState::register_vec3_channel(const StringName &p_key, const Vector3 &p_initial) {
	ERR_FAIL_COND_V(p_key == StringName(), -1);
	ERR_FAIL_COND_V_MSG(is_structure_locked(), -1, "WorldState: Cannot resize SoA channels while structure_locked is true (Invariant #3).");
	RWLockWrite lock(state_rw_lock);
	const uint32_t *existing = scalar_key_to_meta.getptr(p_key);
	if (existing) {
		const ScalarChannelMeta &m = scalar_meta[*existing];
		ERR_FAIL_COND_V(m.type != COLUMN_VEC3, -1);
		return (int32_t)m.dense_index;
	}
	uint32_t col_idx = vec3_column.size();
	vec3_column.push_back(p_initial);

	ScalarChannelMeta meta;
	meta.key = p_key;
	meta.type = COLUMN_VEC3;
	meta.dense_index = col_idx;
	uint32_t meta_idx = scalar_meta.size();
	scalar_meta.push_back(meta);
	scalar_key_to_meta.insert(p_key, meta_idx);
	return (int32_t)col_idx;
}

void WorldState::set_float(const StringName &p_key, double p_value) {
	ERR_FAIL_COND(p_key == StringName());
	{
		RWLockRead read_lock(state_rw_lock);
		const uint32_t *meta_idx = scalar_key_to_meta.getptr(p_key);
		if (meta_idx) {
			const ScalarChannelMeta &m = scalar_meta[*meta_idx];
			if (m.type == COLUMN_FLOAT) {
				float_column[m.dense_index] = p_value;
				return;
			} else if (m.type == COLUMN_INT) {
				int_column[m.dense_index] = (int64_t)p_value;
				return;
			}
		}
	}
	if (is_structure_locked()) {
		MutexLock lock(deferred_structure_mutex);
		pending_scalar_mutations.push_back({ p_key, p_value });
		return;
	}
	register_float_channel(p_key, p_value);
}

double WorldState::get_float(const StringName &p_key, double p_default) const {
	RWLockRead read_lock(state_rw_lock);
	const uint32_t *meta_idx = scalar_key_to_meta.getptr(p_key);
	if (!meta_idx) {
		return p_default;
	}
	const ScalarChannelMeta &m = scalar_meta[*meta_idx];
	if (m.type == COLUMN_FLOAT) {
		return float_column[m.dense_index];
	} else if (m.type == COLUMN_INT) {
		return (double)int_column[m.dense_index];
	}
	return p_default;
}

void WorldState::set_int(const StringName &p_key, int64_t p_value) {
	ERR_FAIL_COND(p_key == StringName());
	{
		RWLockRead read_lock(state_rw_lock);
		const uint32_t *meta_idx = scalar_key_to_meta.getptr(p_key);
		if (meta_idx) {
			const ScalarChannelMeta &m = scalar_meta[*meta_idx];
			if (m.type == COLUMN_INT) {
				int_column[m.dense_index] = p_value;
				return;
			} else if (m.type == COLUMN_FLOAT) {
				float_column[m.dense_index] = (double)p_value;
				return;
			}
		}
	}
	if (is_structure_locked()) {
		MutexLock lock(deferred_structure_mutex);
		pending_scalar_mutations.push_back({ p_key, p_value });
		return;
	}
	register_int_channel(p_key, p_value);
}

int64_t WorldState::get_int(const StringName &p_key, int64_t p_default) const {
	RWLockRead read_lock(state_rw_lock);
	const uint32_t *meta_idx = scalar_key_to_meta.getptr(p_key);
	if (!meta_idx) {
		return p_default;
	}
	const ScalarChannelMeta &m = scalar_meta[*meta_idx];
	if (m.type == COLUMN_INT) {
		return int_column[m.dense_index];
	} else if (m.type == COLUMN_FLOAT) {
		return (int64_t)float_column[m.dense_index];
	}
	return p_default;
}

void WorldState::set_bool(const StringName &p_key, bool p_value) {
	ERR_FAIL_COND(p_key == StringName());
	{
		RWLockRead read_lock(state_rw_lock);
		const uint32_t *meta_idx = scalar_key_to_meta.getptr(p_key);
		if (meta_idx) {
			const ScalarChannelMeta &m = scalar_meta[*meta_idx];
			if (m.type == COLUMN_BOOL) {
				bool_column[m.dense_index] = p_value ? 1 : 0;
				return;
			}
		}
	}
	if (is_structure_locked()) {
		MutexLock lock(deferred_structure_mutex);
		pending_scalar_mutations.push_back({ p_key, p_value });
		return;
	}
	register_bool_channel(p_key, p_value);
}

bool WorldState::get_bool(const StringName &p_key, bool p_default) const {
	RWLockRead read_lock(state_rw_lock);
	const uint32_t *meta_idx = scalar_key_to_meta.getptr(p_key);
	if (!meta_idx) {
		return p_default;
	}
	const ScalarChannelMeta &m = scalar_meta[*meta_idx];
	if (m.type == COLUMN_BOOL) {
		return bool_column[m.dense_index] != 0;
	}
	return p_default;
}

void WorldState::set_string(const StringName &p_key, const String &p_value) {
	ERR_FAIL_COND(p_key == StringName());
	{
		RWLockWrite write_lock(state_rw_lock);
		const uint32_t *meta_idx = scalar_key_to_meta.getptr(p_key);
		if (meta_idx) {
			const ScalarChannelMeta &m = scalar_meta[*meta_idx];
			if (m.type == COLUMN_STRING) {
				string_column[m.dense_index] = p_value;
				return;
			}
		}
	}
	if (is_structure_locked()) {
		MutexLock lock(deferred_structure_mutex);
		pending_scalar_mutations.push_back({ p_key, p_value });
		return;
	}
	register_string_channel(p_key, p_value);
}

String WorldState::get_string(const StringName &p_key, const String &p_default) const {
	RWLockRead read_lock(state_rw_lock);
	const uint32_t *meta_idx = scalar_key_to_meta.getptr(p_key);
	if (!meta_idx) {
		return p_default;
	}
	const ScalarChannelMeta &m = scalar_meta[*meta_idx];
	if (m.type == COLUMN_STRING) {
		return string_column[m.dense_index];
	}
	return p_default;
}

void WorldState::set_vec3(const StringName &p_key, const Vector3 &p_value) {
	ERR_FAIL_COND(p_key == StringName());
	{
		RWLockRead read_lock(state_rw_lock);
		const uint32_t *meta_idx = scalar_key_to_meta.getptr(p_key);
		if (meta_idx) {
			const ScalarChannelMeta &m = scalar_meta[*meta_idx];
			if (m.type == COLUMN_VEC3) {
				vec3_column[m.dense_index] = p_value;
				return;
			}
		}
	}
	if (is_structure_locked()) {
		MutexLock lock(deferred_structure_mutex);
		pending_scalar_mutations.push_back({ p_key, p_value });
		return;
	}
	register_vec3_channel(p_key, p_value);
}

Vector3 WorldState::get_vec3(const StringName &p_key, const Vector3 &p_default) const {
	RWLockRead read_lock(state_rw_lock);
	const uint32_t *meta_idx = scalar_key_to_meta.getptr(p_key);
	if (!meta_idx) {
		return p_default;
	}
	const ScalarChannelMeta &m = scalar_meta[*meta_idx];
	if (m.type == COLUMN_VEC3) {
		return vec3_column[m.dense_index];
	}
	return p_default;
}

void WorldState::set_value(const StringName &p_key, const Variant &p_value) {
	switch (p_value.get_type()) {
		case Variant::FLOAT:
			set_float(p_key, (double)p_value);
			break;
		case Variant::INT:
			set_int(p_key, (int64_t)p_value);
			break;
		case Variant::BOOL:
			set_bool(p_key, (bool)p_value);
			break;
		case Variant::STRING:
		case Variant::STRING_NAME:
			set_string(p_key, p_value.operator String());
			break;
		case Variant::VECTOR3:
			set_vec3(p_key, (Vector3)p_value);
			break;
		default:
			ERR_FAIL_MSG("WorldState::set_value supports primitive SoA types (FLOAT, INT, BOOL, STRING, VECTOR3).");
	}
}

Variant WorldState::get_value(const StringName &p_key, const Variant &p_default) const {
	RWLockRead read_lock(state_rw_lock);
	const uint32_t *meta_idx = scalar_key_to_meta.getptr(p_key);
	if (!meta_idx) {
		return p_default;
	}
	const ScalarChannelMeta &m = scalar_meta[*meta_idx];
	switch (m.type) {
		case COLUMN_FLOAT:
			return float_column[m.dense_index];
		case COLUMN_INT:
			return int_column[m.dense_index];
		case COLUMN_BOOL:
			return bool_column[m.dense_index] != 0;
		case COLUMN_STRING:
			return string_column[m.dense_index];
		case COLUMN_VEC3:
			return vec3_column[m.dense_index];
		default:
			return p_default;
	}
}

void WorldState::interpolate_value(const StringName &p_key, const Variant &p_from, const Variant &p_to, double p_weight) {
	double w = CLAMP(p_weight, 0.0, 1.0);
	if (p_from.get_type() == Variant::FLOAT || p_to.get_type() == Variant::FLOAT) {
		double a = (double)p_from;
		double b = (double)p_to;
		set_float(p_key, Math::lerp(a, b, w));
	} else if (p_from.get_type() == Variant::INT && p_to.get_type() == Variant::INT) {
		double a = (double)(int64_t)p_from;
		double b = (double)(int64_t)p_to;
		set_int(p_key, (int64_t)Math::round(Math::lerp(a, b, w)));
	} else if (p_from.get_type() == Variant::VECTOR3 && p_to.get_type() == Variant::VECTOR3) {
		Vector3 a = p_from;
		Vector3 b = p_to;
		set_vec3(p_key, a.lerp(b, (real_t)w));
	} else {
		set_value(p_key, (w < 0.5) ? p_from : p_to);
	}
}

bool WorldState::has_key(const StringName &p_key) const {
	RWLockRead read_lock(state_rw_lock);
	return scalar_key_to_meta.has(p_key);
}

PackedStringArray WorldState::get_all_keys() const {
	RWLockRead read_lock(state_rw_lock);
	PackedStringArray out;
	for (uint32_t i = 0; i < scalar_meta.size(); i++) {
		out.push_back(String(scalar_meta[i].key));
	}
	out.sort();
	return out;
}

uint32_t WorldState::get_scalar_channel_count() const {
	RWLockRead read_lock(state_rw_lock);
	return scalar_meta.size();
}

int32_t WorldState::_allocate_entity_row_unlocked(RID p_rid, double p_x, double p_y, double p_z, const Vector3 &p_vel, float p_health, uint32_t p_flags) {
	uint64_t id = p_rid.get_id();
	const uint32_t *existing = entity_rid_to_row.getptr(id);
	if (existing) {
		uint32_t row = *existing;
		entity_pos_x[row] = p_x;
		entity_pos_y[row] = p_y;
		entity_pos_z[row] = p_z;
		entity_vel[row] = p_vel;
		entity_health[row] = p_health;
		entity_flags[row] = p_flags;
		return (int32_t)row;
	}

	uint32_t row = entity_rids.size();
	entity_rids.push_back(p_rid);
	entity_pos_x.push_back(p_x);
	entity_pos_y.push_back(p_y);
	entity_pos_z.push_back(p_z);
	entity_vel.push_back(p_vel);
	entity_health.push_back(p_health);
	entity_flags.push_back(p_flags);
	entity_rid_to_row.insert(id, row);
	return (int32_t)row;
}

bool WorldState::_remove_entity_row_unlocked(RID p_rid) {
	uint64_t id = p_rid.get_id();
	const uint32_t *existing = entity_rid_to_row.getptr(id);
	if (!existing) {
		return false;
	}
	uint32_t row = *existing;
	uint32_t last_row = entity_rids.size() - 1;

	if (row != last_row) {
		RID moved_rid = entity_rids[last_row];
		entity_rids[row] = moved_rid;
		entity_pos_x[row] = entity_pos_x[last_row];
		entity_pos_y[row] = entity_pos_y[last_row];
		entity_pos_z[row] = entity_pos_z[last_row];
		entity_vel[row] = entity_vel[last_row];
		entity_health[row] = entity_health[last_row];
		entity_flags[row] = entity_flags[last_row];
		entity_rid_to_row.insert(moved_rid.get_id(), row);
	}

	entity_rids.remove_at(last_row);
	entity_pos_x.remove_at(last_row);
	entity_pos_y.remove_at(last_row);
	entity_pos_z.remove_at(last_row);
	entity_vel.remove_at(last_row);
	entity_health.remove_at(last_row);
	entity_flags.remove_at(last_row);
	entity_rid_to_row.erase(id);
	return true;
}

int32_t WorldState::allocate_entity_row(RID p_rid, double p_x, double p_y, double p_z, const Vector3 &p_vel, float p_health, uint32_t p_flags) {
	ERR_FAIL_COND_V(!p_rid.is_valid(), -1);
	ERR_FAIL_COND_V_MSG(is_structure_locked(), -1, "WorldState: Cannot resize Entity SoA table during parallel WorkerThreadPool tick (Invariant #3). Use queue_entity_spawn().");
	RWLockWrite write_lock(state_rw_lock);
	return _allocate_entity_row_unlocked(p_rid, p_x, p_y, p_z, p_vel, p_health, p_flags);
}

bool WorldState::remove_entity_row(RID p_rid) {
	ERR_FAIL_COND_V(!p_rid.is_valid(), false);
	ERR_FAIL_COND_V_MSG(is_structure_locked(), false, "WorldState: Cannot shrink Entity SoA table during parallel WorkerThreadPool tick (Invariant #3). Use queue_entity_removal().");
	RWLockWrite write_lock(state_rw_lock);
	return _remove_entity_row_unlocked(p_rid);
}

void WorldState::queue_entity_spawn(RID p_rid, double p_x, double p_y, double p_z, const Vector3 &p_vel, float p_health, uint32_t p_flags) {
	ERR_FAIL_COND(!p_rid.is_valid());
	MutexLock lock(deferred_structure_mutex);
	pending_entity_spawns.push_back({ p_rid, p_x, p_y, p_z, p_vel, p_health, p_flags });
}

void WorldState::queue_entity_removal(RID p_rid) {
	ERR_FAIL_COND(!p_rid.is_valid());
	MutexLock lock(deferred_structure_mutex);
	pending_entity_removals.push_back(p_rid);
}

int32_t WorldState::get_entity_row_index(RID p_rid) const {
	RWLockRead read_lock(state_rw_lock);
	const uint32_t *row = entity_rid_to_row.getptr(p_rid.get_id());
	return row ? (int32_t)(*row) : -1;
}

void WorldState::set_entity_position_64(RID p_rid, double p_x, double p_y, double p_z) {
	RWLockRead read_lock(state_rw_lock);
	const uint32_t *row = entity_rid_to_row.getptr(p_rid.get_id());
	ERR_FAIL_NULL(row);
	entity_pos_x[*row] = p_x;
	entity_pos_y[*row] = p_y;
	entity_pos_z[*row] = p_z;
}

Vector3 WorldState::get_entity_position_local(RID p_rid) const {
	RWLockRead read_lock(state_rw_lock);
	const uint32_t *row = entity_rid_to_row.getptr(p_rid.get_id());
	ERR_FAIL_NULL_V(row, Vector3());
	return Vector3(
			(real_t)(entity_pos_x[*row] - world_origin_64[0]),
			(real_t)(entity_pos_y[*row] - world_origin_64[1]),
			(real_t)(entity_pos_z[*row] - world_origin_64[2]));
}

void WorldState::set_entity_velocity(RID p_rid, const Vector3 &p_vel) {
	RWLockRead read_lock(state_rw_lock);
	const uint32_t *row = entity_rid_to_row.getptr(p_rid.get_id());
	ERR_FAIL_NULL(row);
	entity_vel[*row] = p_vel;
}

Vector3 WorldState::get_entity_velocity(RID p_rid) const {
	RWLockRead read_lock(state_rw_lock);
	const uint32_t *row = entity_rid_to_row.getptr(p_rid.get_id());
	ERR_FAIL_NULL_V(row, Vector3());
	return entity_vel[*row];
}

void WorldState::set_entity_health(RID p_rid, float p_health) {
	RWLockRead read_lock(state_rw_lock);
	const uint32_t *row = entity_rid_to_row.getptr(p_rid.get_id());
	ERR_FAIL_NULL(row);
	entity_health[*row] = p_health;
}

float WorldState::get_entity_health(RID p_rid) const {
	RWLockRead read_lock(state_rw_lock);
	const uint32_t *row = entity_rid_to_row.getptr(p_rid.get_id());
	ERR_FAIL_NULL_V(row, 0.0f);
	return entity_health[*row];
}

void WorldState::set_world_origin_64(double p_x, double p_y, double p_z) {
	RWLockWrite write_lock(state_rw_lock);
	world_origin_64[0] = p_x;
	world_origin_64[1] = p_y;
	world_origin_64[2] = p_z;
}

void WorldState::get_world_origin_64(double &r_x, double &r_y, double &r_z) const {
	RWLockRead read_lock(state_rw_lock);
	r_x = world_origin_64[0];
	r_y = world_origin_64[1];
	r_z = world_origin_64[2];
}

void WorldState::set_world_origin_64_array(const Array &p_origin) {
	ERR_FAIL_COND(p_origin.size() < 3);
	set_world_origin_64((double)p_origin[0], (double)p_origin[1], (double)p_origin[2]);
}

Array WorldState::get_world_origin_64_array() const {
	RWLockRead read_lock(state_rw_lock);
	Array arr;
	arr.push_back(world_origin_64[0]);
	arr.push_back(world_origin_64[1]);
	arr.push_back(world_origin_64[2]);
	return arr;
}

Dictionary WorldState::to_dictionary() const {
	RWLockRead read_lock(state_rw_lock);
	Dictionary d;
	for (uint32_t i = 0; i < scalar_meta.size(); i++) {
		const ScalarChannelMeta &m = scalar_meta[i];
		switch (m.type) {
			case COLUMN_FLOAT:
				d[m.key] = float_column[m.dense_index];
				break;
			case COLUMN_INT:
				d[m.key] = int_column[m.dense_index];
				break;
			case COLUMN_BOOL:
				d[m.key] = (bool_column[m.dense_index] != 0);
				break;
			case COLUMN_STRING:
				d[m.key] = string_column[m.dense_index];
				break;
			case COLUMN_VEC3: {
				const Vector3 &v = vec3_column[m.dense_index];
				Array arr;
				arr.push_back(v.x);
				arr.push_back(v.y);
				arr.push_back(v.z);
				d[m.key] = arr;
				break;
			}
		}
	}
	return d;
}

Error WorldState::from_dictionary(const Dictionary &p_dict) {
	ERR_FAIL_COND_V_MSG(is_structure_locked(), ERR_LOCKED, "Cannot restore WorldState while structure is locked.");
	LocalVector<Variant> keys = p_dict.get_key_list();
	for (uint32_t i = 0; i < keys.size(); i++) {
		const Variant &k = keys[i];
		if (k.get_type() != Variant::STRING && k.get_type() != Variant::STRING_NAME) {
			return ERR_INVALID_DATA;
		}
		StringName key_name(k.operator String());
		const Variant &val = p_dict[k];
		switch (val.get_type()) {
			case Variant::FLOAT: {
				double f = (double)val;
				if (Math::is_nan(f) || Math::is_inf(f)) {
					return ERR_INVALID_DATA;
				}
				set_float(key_name, f);
				break;
			}
			case Variant::INT:
				set_int(key_name, (int64_t)val);
				break;
			case Variant::BOOL:
				set_bool(key_name, (bool)val);
				break;
			case Variant::STRING:
			case Variant::STRING_NAME:
				set_string(key_name, val.operator String());
				break;
			case Variant::VECTOR3:
				set_vec3(key_name, (Vector3)val);
				break;
			case Variant::ARRAY: {
				Array arr = val;
				if (arr.size() == 3 && (arr[0].get_type() == Variant::FLOAT || arr[0].get_type() == Variant::INT)) {
					set_vec3(key_name, Vector3((real_t)(double)arr[0], (real_t)(double)arr[1], (real_t)(double)arr[2]));
				} else {
					return ERR_INVALID_DATA;
				}
				break;
			}
			default:
				// Reject Object or unsupported complex variants (Security §5).
				return ERR_INVALID_DATA;
		}
	}
	return OK;
}

void WorldState::clear() {
	ERR_FAIL_COND_MSG(is_structure_locked(), "WorldState::clear cannot be called while structure_locked is true.");
	RWLockWrite write_lock(state_rw_lock);
	scalar_key_to_meta.clear();
	scalar_meta.clear();
	float_column.clear();
	int_column.clear();
	bool_column.clear();
	string_column.clear();
	vec3_column.clear();

	entity_rid_to_row.clear();
	entity_rids.clear();
	entity_pos_x.clear();
	entity_pos_y.clear();
	entity_pos_z.clear();
	entity_vel.clear();
	entity_health.clear();
	entity_flags.clear();

	world_origin_64[0] = 0.0;
	world_origin_64[1] = 0.0;
	world_origin_64[2] = 0.0;
}

void WorldState::_bind_methods() {
	ClassDB::bind_method(D_METHOD("lock_structure"), &WorldState::lock_structure);
	ClassDB::bind_method(D_METHOD("unlock_and_commit_structural_changes"), &WorldState::unlock_and_commit_structural_changes);
	ClassDB::bind_method(D_METHOD("is_structure_locked"), &WorldState::is_structure_locked);

	ClassDB::bind_method(D_METHOD("register_float_channel", "key", "initial"), &WorldState::register_float_channel, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("register_int_channel", "key", "initial"), &WorldState::register_int_channel, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("register_bool_channel", "key", "initial"), &WorldState::register_bool_channel, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("register_string_channel", "key", "initial"), &WorldState::register_string_channel, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("register_vec3_channel", "key", "initial"), &WorldState::register_vec3_channel, DEFVAL(Vector3()));

	ClassDB::bind_method(D_METHOD("set_float", "key", "value"), &WorldState::set_float);
	ClassDB::bind_method(D_METHOD("get_float", "key", "default"), &WorldState::get_float, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("set_int", "key", "value"), &WorldState::set_int);
	ClassDB::bind_method(D_METHOD("get_int", "key", "default"), &WorldState::get_int, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("set_bool", "key", "value"), &WorldState::set_bool);
	ClassDB::bind_method(D_METHOD("get_bool", "key", "default"), &WorldState::get_bool, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("set_string", "key", "value"), &WorldState::set_string);
	ClassDB::bind_method(D_METHOD("get_string", "key", "default"), &WorldState::get_string, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("set_vec3", "key", "value"), &WorldState::set_vec3);
	ClassDB::bind_method(D_METHOD("get_vec3", "key", "default"), &WorldState::get_vec3, DEFVAL(Vector3()));

	ClassDB::bind_method(D_METHOD("set_value", "key", "value"), &WorldState::set_value);
	ClassDB::bind_method(D_METHOD("get_value", "key", "default"), &WorldState::get_value, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("interpolate_value", "key", "from", "to", "weight"), &WorldState::interpolate_value);

	ClassDB::bind_method(D_METHOD("has_key", "key"), &WorldState::has_key);
	ClassDB::bind_method(D_METHOD("get_all_keys"), &WorldState::get_all_keys);
	ClassDB::bind_method(D_METHOD("get_scalar_channel_count"), &WorldState::get_scalar_channel_count);

	ClassDB::bind_method(D_METHOD("allocate_entity_row", "rid", "x", "y", "z", "vel", "health", "flags"), &WorldState::allocate_entity_row, DEFVAL(0.0), DEFVAL(0.0), DEFVAL(0.0), DEFVAL(Vector3()), DEFVAL(100.0f), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("remove_entity_row", "rid"), &WorldState::remove_entity_row);
	ClassDB::bind_method(D_METHOD("queue_entity_spawn", "rid", "x", "y", "z", "vel", "health", "flags"), &WorldState::queue_entity_spawn, DEFVAL(Vector3()), DEFVAL(100.0f), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("queue_entity_removal", "rid"), &WorldState::queue_entity_removal);
	ClassDB::bind_method(D_METHOD("get_entity_row_index", "rid"), &WorldState::get_entity_row_index);
	ClassDB::bind_method(D_METHOD("get_entity_count"), &WorldState::get_entity_count);
	ClassDB::bind_method(D_METHOD("set_entity_position_64", "rid", "x", "y", "z"), &WorldState::set_entity_position_64);
	ClassDB::bind_method(D_METHOD("get_entity_position_local", "rid"), &WorldState::get_entity_position_local);
	ClassDB::bind_method(D_METHOD("set_entity_velocity", "rid", "vel"), &WorldState::set_entity_velocity);
	ClassDB::bind_method(D_METHOD("get_entity_velocity", "rid"), &WorldState::get_entity_velocity);
	ClassDB::bind_method(D_METHOD("set_entity_health", "rid", "health"), &WorldState::set_entity_health);
	ClassDB::bind_method(D_METHOD("get_entity_health", "rid"), &WorldState::get_entity_health);

	ClassDB::bind_method(D_METHOD("set_world_origin_64_array", "origin"), &WorldState::set_world_origin_64_array);
	ClassDB::bind_method(D_METHOD("get_world_origin_64_array"), &WorldState::get_world_origin_64_array);

	ClassDB::bind_method(D_METHOD("to_dictionary"), &WorldState::to_dictionary);
	ClassDB::bind_method(D_METHOD("from_dictionary", "dict"), &WorldState::from_dictionary);
	ClassDB::bind_method(D_METHOD("clear"), &WorldState::clear);
}

WorldState::WorldState() {
	singleton = this;
}

WorldState::~WorldState() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
