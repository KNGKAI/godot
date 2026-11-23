/**************************************************************************/
/*  streaming_server.cpp                                                  */
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

#include "streaming_server.h"

#include "origin_shifter.h"
#include "streaming_invoker.h"

#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "scene/main/scene_tree.h"
#include "servers/rendering/rendering_server.h"

#include "modules/sandbox_core/command_bus.h"
#include "modules/sandbox_core/event_bus.h"
#include "modules/sandbox_core/sandbox_clock.h"
#include "modules/sandbox_core/sandbox_serializer.h"

Error StreamingServer::set_manifest(const Ref<WorldPartitionManifest> &p_manifest) {
	ERR_FAIL_COND_V(!Thread::is_main_thread() || updating, ERR_BUSY);
	if (p_manifest.is_valid() && p_manifest->validate() != OK) {
		return ERR_INVALID_DATA;
	}

	updating = true;
	_clear_residency();
	layers.clear();
	cells.clear();
	pins.clear();
	if (p_manifest.is_valid()) {
		// Snapshot the validated descriptors. Inspector edits require explicit re-registration.
		for (const Ref<WorldPartitionLayer> layer : p_manifest->get_layers()) {
			Layer &entry = layers[layer->get_layer_id()];
			entry.size = layer->get_cell_size();
			entry.load_radius = layer->get_load_radius();
			entry.evict_radius = layer->get_evict_radius();
		}
		for (const Ref<WorldPartitionCell> cell : p_manifest->get_cells()) {
			Cell entry;
			entry.id = cell->get_cell_id();
			entry.layer = cell->get_layer();
			entry.coords = cell->get_grid_coords();
			entry.paths = { cell->get_hlod_impostor_path(), cell->get_mid_mesh_path(), cell->get_high_scene_path(), cell->get_collision_bundle_path() };
			layers[entry.layer].grid.insert(entry.coords, cells.size());
			cells.push_back(entry);
		}
	}
	updating = false;
	return OK;
}

void StreamingServer::register_invoker(StreamingInvoker *p_invoker) {
	ERR_FAIL_COND(!Thread::is_main_thread());
	ERR_FAIL_NULL(p_invoker);
	if (!invokers.has(p_invoker->get_instance_id())) {
		invokers.push_back(p_invoker->get_instance_id());
	}
}

void StreamingServer::unregister_invoker(StreamingInvoker *p_invoker) {
	ERR_FAIL_COND(!Thread::is_main_thread());
	ERR_FAIL_NULL(p_invoker);
	invokers.erase(p_invoker->get_instance_id());
}

void StreamingServer::set_enabled(bool p_enabled) {
	ERR_FAIL_COND(!Thread::is_main_thread());
	if (updating) {
		callable_mp(this, &StreamingServer::set_enabled).call_deferred(p_enabled);
		return;
	}
	if (enabled == p_enabled) {
		return;
	}
	enabled = p_enabled;
	if (SandboxClock *clock = SandboxClock::get_singleton()) {
		clock->set_streaming_update_callback(enabled ? _frame : nullptr);
		clock->set_physics_start_callback(enabled ? _physics_start : nullptr);
	}
	if (SandboxSerializer *serializer = SandboxSerializer::get_singleton()) {
		serializer->set_streaming_diffs_callbacks(nullptr, enabled ? _restore_diffs : nullptr);
	}
	if (!enabled) {
		updating = true;
		_clear_residency();
		updating = false;
	}
}

Error StreamingServer::_restore_diffs(const Dictionary &p_diffs) {
	// The serializer has restored both the sparse byte arrays and WorldState's double origin.
	// The serializer remains the sole owner of these sparse arrays during eviction and rewind.
	OriginShifter::get_singleton()->sync_from_world_state();
	return OK;
}

void StreamingServer::_clear_residency() {
	for (const KeyValue<int, CellResidency *> &entry : residents) {
		memdelete(entry.value);
	}
	residents.clear();
	Node3D *root = ObjectDB::get_instance<Node3D>(stream_root);
	if (root) {
		root->queue_free();
	}
	stream_root = ObjectID();
	scene_id = ObjectID();
	queued_count = 0;
	last_activation_usec = 0;
}

Node3D *StreamingServer::_get_stream_root() {
	SceneTree *tree = SceneTree::get_singleton();
	Node *scene = tree ? tree->get_current_scene() : nullptr;
	if (scene_id.is_valid() && (!scene || scene->get_instance_id() != scene_id || !ObjectDB::get_instance(stream_root))) {
		_clear_residency();
	}
	if (!scene || !scene->is_inside_tree() || scene->is_queued_for_deletion()) {
		return nullptr;
	}
	Node3D *root = ObjectDB::get_instance<Node3D>(stream_root);
	if (!root) {
		root = memnew(Node3D);
		root->set_name(SNAME("StreamedCells"));
		root->set_as_top_level(true);
		scene->add_child(root, false, Node::INTERNAL_MODE_BACK);
		root->set_global_transform(Transform3D());
		stream_root = root->get_instance_id();
		scene_id = scene->get_instance_id();
	}
	return root;
}

void StreamingServer::_frame(double p_delta) {
	singleton->update(p_delta);
}

void StreamingServer::_physics_start(double p_delta) {
	if (!singleton->invokers.is_empty()) {
		auto *invoker = ObjectDB::get_instance<StreamingInvoker>(singleton->invokers[0]);
		if (invoker && invoker->is_inside_tree()) {
			OriginShifter::get_singleton()->check_and_shift(invoker);
		}
	}
}

CellResidency::Lod StreamingServer::_select_lod(const Cell &p_cell, double p_distance, double p_radius) const {
	const auto &paths = p_cell.paths;
	if (forced_hlod != 0 && !paths.proxy.is_empty() && (forced_hlod == 1 || p_distance > p_radius * 0.75)) {
		return CellResidency::LOD_PROXY;
	}
	if (!paths.mid.is_empty() && (p_distance > p_radius * 0.4 || paths.high.is_empty())) {
		return CellResidency::LOD_MID;
	}
	if (!paths.high.is_empty() || !paths.collision.is_empty()) {
		return CellResidency::LOD_HIGH;
	}
	return CellResidency::LOD_PROXY;
}

void StreamingServer::_evaluate(StreamingInvoker *p_invoker, const PackedFloat64Array &p_origin, HashMap<int, Demand> &r_wanted) const {
	const Vector3 local = p_invoker->get_global_position();
	const Vector3 ahead = p_invoker->get_velocity() * p_invoker->get_lookahead();
	const Vector3 forward = ahead.is_zero_approx() ? -p_invoker->get_global_transform().basis.get_column(2).normalized() : ahead.normalized();
	const double x = p_origin[0] + local.x;
	const double z = p_origin[2] + local.z;
	const double dx = ahead.x;
	const double dz = ahead.z;
	const double segment_squared = dx * dx + dz * dz;
	if (!Math::is_finite(x) || !Math::is_finite(z) || !Math::is_finite(segment_squared)) {
		return;
	}
	for (const KeyValue<StringName, Layer> &entry : layers) {
		const Layer &layer = entry.value;
		// Evaluate cell centers along the lookahead segment, including the current position.
		auto consider = [&](int p_index) {
			const Cell &cell = cells[p_index];
			const double cx = (cell.coords.x + 0.5) * layer.size - x;
			const double cz = (cell.coords.y + 0.5) * layer.size - z;
			const double t = segment_squared > 0 ? CLAMP((cx * dx + cz * dz) / segment_squared, 0.0, 1.0) : 0;
			const double near_x = MAX(0.0, Math::abs(cx - t * dx) - layer.size * 0.5);
			const double near_z = MAX(0.0, Math::abs(cz - t * dz) - layer.size * 0.5);
			const double distance = Math::sqrt(near_x * near_x + near_z * near_z);
			if (distance > (residents.has(p_index) ? layer.evict_radius : layer.load_radius)) {
				return;
			}
			const double center_distance = Math::sqrt(cx * cx + cz * cz);
			const double facing = (cx * forward.x + cz * forward.z) / MAX(1.0, center_distance);
			const double priority = 1.0 / MAX(1.0, center_distance) + MAX(0.0, facing);
			Demand *existing = r_wanted.getptr(p_index);
			if (!existing) {
				r_wanted.insert(p_index, { distance, priority, _select_lod(cell, distance, layer.load_radius) });
			} else {
				existing->distance = MIN(existing->distance, distance);
				existing->lod = _select_lod(cell, existing->distance, layer.load_radius);
				existing->priority = MAX(existing->priority, priority);
			}
		};

		// Iterate the cheaper of a bounded coordinate range and a sparse layer's occupied cells.
		const double min_x = MAX(double(INT32_MIN), Math::floor((x + MIN(0.0, dx) - layer.evict_radius) / layer.size) - 1.0);
		const double max_x = MIN(double(INT32_MAX), Math::floor((x + MAX(0.0, dx) + layer.evict_radius) / layer.size));
		const double min_z = MAX(double(INT32_MIN), Math::floor((z + MIN(0.0, dz) - layer.evict_radius) / layer.size) - 1.0);
		const double max_z = MIN(double(INT32_MAX), Math::floor((z + MAX(0.0, dz) + layer.evict_radius) / layer.size));
		if (min_x > max_x || min_z > max_z) {
			continue;
		}
		if ((max_x - min_x + 1) * (max_z - min_z + 1) > layer.grid.size()) {
			for (const KeyValue<Vector2i, int> &cell : layer.grid) {
				consider(cell.value);
			}
		} else {
			for (int64_t gx = int64_t(min_x); gx <= int64_t(max_x); gx++) {
				for (int64_t gz = int64_t(min_z); gz <= int64_t(max_z); gz++) {
					const int *index = layer.grid.getptr(Vector2i(gx, gz));
					if (index) {
						consider(*index);
					}
				}
			}
		}
	}
}

void StreamingServer::update(double p_delta) {
	ERR_FAIL_COND(!Thread::is_main_thread());
	if (!enabled || updating || (residents.is_empty() && (cells.is_empty() || (pins.is_empty() && invokers.is_empty())))) {
		return;
	}
	updating = true;
	if (!_get_stream_root()) {
		updating = false;
		return;
	}

	OriginShifter *shifter = OriginShifter::get_singleton();
	shifter->sync_from_world_state();
	const PackedFloat64Array origin = shifter->get_origin_64();
	HashMap<int, Demand> wanted;
	for (ObjectID id : invokers) {
		StreamingInvoker *invoker = ObjectDB::get_instance<StreamingInvoker>(id);
		if (invoker && invoker->is_inside_tree() && !invoker->is_queued_for_deletion()) {
			invoker->sample_for_streaming(p_delta);
			_evaluate(invoker, origin, wanted);
		}
	}
	for (int index : pins) {
		Demand &demand = wanted[index];
		demand.priority += 4.0;
		demand.lod = _select_lod(cells[index], 0, layers[cells[index].layer].load_radius);
	}

	LocalVector<Candidate> queue;
	queued_count = 0;
	for (const KeyValue<int, Demand> &entry : wanted) {
		queue.push_back({ entry.key, entry.value });
		queued_count += !residents.has(entry.key);
	}
	queue.sort();
	const uint64_t start = OS::get_singleton()->get_ticks_usec();
	const uint64_t deadline = start + budget_usec;
	// Keep most of the frame budget available for an indivisible subtree.
	// A Vulkan trace measured a single SceneTree attachment at 1.61 ms.
	CellResidency::SceneStep scene_step{ start + MAX(uint64_t(1), budget_usec / 8) };
	// Poll departing cells as well; every successful async request must be collected.
	LocalVector<int> evicted;
	int pending = 0;
	for (const KeyValue<int, CellResidency *> &entry : residents) {
		if (!wanted.has(entry.key) && OS::get_singleton()->get_ticks_usec() < deadline) {
			entry.value->tick(CellResidency::LOD_NONE, cells[entry.key].paths, nullptr, Vector3(), p_delta, deadline);
			if (entry.value->get_state() == CellResidency::UNLOADED) {
				evicted.push_back(entry.key);
			}
		}
		pending += entry.value->has_pending_request();
	}
	for (int index : evicted) {
		memdelete(residents[index]);
		residents.erase(index);
	}

	for (const Candidate &candidate : queue) {
		if (OS::get_singleton()->get_ticks_usec() >= deadline) {
			break;
		}
		Node3D *root = ObjectDB::get_instance<Node3D>(stream_root);
		if (!root || !root->is_inside_tree() || root->is_queued_for_deletion()) {
			break;
		}
		const int index = candidate.index;
		const Cell &cell = cells[index];
		CellResidency **existing = residents.getptr(index);
		if (!existing && pending >= 8) {
			continue;
		}
		CellResidency *residency = existing ? *existing : memnew(CellResidency);
		if (!existing) {
			residents.insert(index, residency);
			queued_count--;
		}
		const double size = layers[cell.layer].size;
		const Vector3 local_world(cell.coords.x * size - origin[0], -origin[1], cell.coords.y * size - origin[2]);
		const bool was_loading = residency->has_pending_request();
		if (residency->tick(candidate.demand.lod, cell.paths, root, local_world, p_delta, deadline, &scene_step)) {
			Dictionary payload;
			payload["cell_id"] = cell.id;
			payload["layer"] = cell.layer;
			payload["diff"] = SandboxSerializer::get_singleton()->get_streaming_cell_diff(cell.id);
			EventBus::get_singleton()->dispatch_deferred(SNAME("stream_cell_activated"), payload);
		}
		pending += int(residency->has_pending_request()) - int(was_loading);
	}
	last_activation_usec = OS::get_singleton()->get_ticks_usec() - start;
	updating = false;
}

Error StreamingServer::set_pinned(const StringName &p_layer, const Vector2i &p_coords, bool p_pinned) {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), ERR_BUSY);
	const Layer *layer = layers.getptr(p_layer);
	const int *index = layer ? layer->grid.getptr(p_coords) : nullptr;
	if (!index) {
		return ERR_DOES_NOT_EXIST;
	}
	if (p_pinned) {
		pins.insert(*index);
	} else {
		pins.erase(*index);
	}
	return OK;
}

Error StreamingServer::set_budget_ms(double p_ms) {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), ERR_BUSY);
	if (!Math::is_finite(p_ms) || p_ms < 0.001 || p_ms > 2.0) {
		return ERR_INVALID_PARAMETER;
	}
	budget_usec = uint64_t(Math::round(p_ms * 1000.0));
	return OK;
}

Error StreamingServer::set_hlod_force(const String &p_mode) {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), ERR_BUSY);
	if (p_mode != "auto" && p_mode != "0" && p_mode != "1") {
		return ERR_INVALID_PARAMETER;
	}
	forced_hlod = p_mode == "auto" ? -1 : p_mode.to_int();
	return OK;
}

#ifdef DEBUG_ENABLED
Dictionary StreamingServer::get_debug_cells() const {
	Dictionary by_layer;
	for (const KeyValue<int, CellResidency *> &entry : residents) {
		const Cell &cell = cells[entry.key];
		Dictionary layer = by_layer.get(cell.layer, Dictionary());
		layer[cell.coords] = int(entry.value->get_active_lod());
		by_layer[cell.layer] = layer;
	}
	return by_layer;
}
#endif

Dictionary StreamingServer::get_stats() const {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), Dictionary());
	int active = 0;
	int loading = 0;
	int failed = 0;
	for (const KeyValue<int, CellResidency *> &entry : residents) {
		active += entry.value->get_active_lod() != CellResidency::LOD_NONE;
		loading += entry.value->has_pending_request();
		failed += entry.value->get_last_error() != OK;
	}
	Dictionary stats;
	stats["enabled"] = enabled;
	stats["resident_cells"] = active;
	stats["loading_cells"] = loading;
	stats["failed_cells"] = failed;
	stats["queue_depth"] = queued_count;
	stats["activation_ms"] = last_activation_usec / 1000.0;
	stats["budget_ms"] = budget_usec / 1000.0;
	stats["renderer_video_memory_bytes"] = RS::get_singleton() ? RS::get_singleton()->get_rendering_info(RSE::RENDERING_INFO_VIDEO_MEM_USED) : 0;
	return stats;
}

static Dictionary command_result(Error p_error) {
	Dictionary result;
	result["ok"] = p_error == OK;
	result["output"] = error_names[p_error];
	result["error_code"] = p_error;
	return result;
}

Variant StreamingServer::_cmd_pin(const StringName &p_layer, int p_x, int p_z) {
	return command_result(set_pinned(p_layer, Vector2i(p_x, p_z), true));
}

Variant StreamingServer::_cmd_unpin(const StringName &p_layer, int p_x, int p_z) {
	return command_result(set_pinned(p_layer, Vector2i(p_x, p_z), false));
}

Variant StreamingServer::_cmd_budget(double p_ms) {
	return command_result(set_budget_ms(p_ms));
}

Variant StreamingServer::_cmd_force(const String &p_mode) {
	return command_result(set_hlod_force(p_mode));
}

StreamingServer::StreamingServer() {
	singleton = this;
	CommandBus *bus = CommandBus::get_singleton();
	bus->register_command_native(SNAME("stream.stats"), CommandBus::PERMISSION_USER, {}, callable_mp(this, &StreamingServer::get_stats));
	bus->register_command_native(SNAME("stream.budget"), CommandBus::PERMISSION_ADMIN, { Variant::FLOAT }, callable_mp(this, &StreamingServer::_cmd_budget));
	const Vector<Variant::Type> coords = { Variant::STRING_NAME, Variant::INT, Variant::INT };
	bus->register_command_native(SNAME("stream.pin"), CommandBus::PERMISSION_ADMIN, coords, callable_mp(this, &StreamingServer::_cmd_pin));
	bus->register_command_native(SNAME("stream.unpin"), CommandBus::PERMISSION_ADMIN, coords, callable_mp(this, &StreamingServer::_cmd_unpin));
	bus->register_command_native(SNAME("stream.hlod_force"), CommandBus::PERMISSION_ADMIN, { Variant::STRING }, callable_mp(this, &StreamingServer::_cmd_force));
}

StreamingServer::~StreamingServer() {
	set_enabled(false);
	if (CommandBus *bus = CommandBus::get_singleton()) {
		for (const char *name : { "stream.stats", "stream.budget", "stream.pin", "stream.unpin", "stream.hlod_force" }) {
			bus->unregister_command(name);
		}
	}
	singleton = nullptr;
}

void StreamingServer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &StreamingServer::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &StreamingServer::is_enabled);
	ClassDB::bind_method(D_METHOD("set_manifest", "manifest"), &StreamingServer::set_manifest);
	ClassDB::bind_method(D_METHOD("set_pinned", "layer", "coords", "pinned"), &StreamingServer::set_pinned);
	ClassDB::bind_method(D_METHOD("set_budget_ms", "milliseconds"), &StreamingServer::set_budget_ms);
	ClassDB::bind_method(D_METHOD("set_hlod_force", "mode"), &StreamingServer::set_hlod_force);
	ClassDB::bind_method(D_METHOD("get_stats"), &StreamingServer::get_stats);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
}
