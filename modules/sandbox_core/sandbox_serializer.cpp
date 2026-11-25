/**************************************************************************/
/*  sandbox_serializer.cpp                                                */
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

#include "sandbox_serializer.h"

#include "event_bus.h"
#include "sandbox_clock.h"
#include "world_state.h"

#include "core/crypto/crypto_core.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/marshalls.h"
#include "core/io/stream_peer.h"
#include "core/math/math_funcs.h"

static const uint8_t SBSAV_MAGIC[8] = { 'S', 'B', 'S', 'A', 'V', 'v', '0', '2' };

bool SandboxSerializer::_contains_forbidden_object_or_nan(const Variant &p_var, int p_depth) {
	if (p_depth > 32) {
		return true;
	}
	switch (p_var.get_type()) {
		case Variant::OBJECT:
		case Variant::CALLABLE:
		case Variant::SIGNAL:
		case Variant::RID:
			return true;
		case Variant::FLOAT: {
			double d = (double)p_var;
			return Math::is_nan(d) || Math::is_inf(d);
		}
		case Variant::STRING:
		case Variant::STRING_NAME: {
			String s = p_var.operator String();
			// Reject embedded text-serialized Godot Object/Resource payloads.
			if (s.contains("Object(") || s.contains("[gd_resource") || s.contains("GDScript")) {
				return true;
			}
			return false;
		}
		case Variant::ARRAY: {
			Array arr = p_var;
			for (int i = 0; i < arr.size(); i++) {
				if (_contains_forbidden_object_or_nan(arr[i], p_depth + 1)) {
					return true;
				}
			}
			return false;
		}
		case Variant::DICTIONARY: {
			Dictionary dict = p_var;
			LocalVector<Variant> keys = dict.get_key_list();
			for (uint32_t i = 0; i < keys.size(); i++) {
				if (_contains_forbidden_object_or_nan(keys[i], p_depth + 1)) {
					return true;
				}
				String ks = keys[i].operator String();
				if (ks == "_class" || ks == "script" || ks == "@path") {
					return true;
				}
				if (_contains_forbidden_object_or_nan(dict[keys[i]], p_depth + 1)) {
					return true;
				}
			}
			return false;
		}
		default:
			return false;
	}
}

Error SandboxSerializer::deserialize_variant_safe(const PackedByteArray &p_bytes, Variant &r_out) {
	if (p_bytes.size() < 4) {
		return ERR_INVALID_DATA;
	}
	// Check raw header type tag before decoding: reject Variant::OBJECT (24) directly.
	uint32_t raw_header = decode_uint32(p_bytes.ptr());
	if ((raw_header & 0xFF) == Variant::OBJECT) {
		return ERR_INVALID_DATA;
	}

	int consumed = 0;
	// Strictly pass p_allow_objects = false (§4.1 & §5).
	Error err = decode_variant(r_out, p_bytes.ptr(), p_bytes.size(), &consumed, /*p_allow_objects=*/false);
	if (err != OK) {
		return ERR_INVALID_DATA;
	}
	if (_contains_forbidden_object_or_nan(r_out)) {
		r_out = Variant();
		return ERR_INVALID_DATA;
	}
	return OK;
}

String SandboxSerializer::_bytes_to_b64(const PackedByteArray &p_bytes) {
	if (p_bytes.is_empty()) {
		return String();
	}
	return CryptoCore::b64_encode_str(p_bytes.ptr(), p_bytes.size());
}

PackedByteArray SandboxSerializer::_b64_to_bytes(const String &p_b64, Error &r_err) {
	r_err = OK;
	PackedByteArray out;
	if (p_b64.is_empty()) {
		return out;
	}
	CharString cs = p_b64.ascii();
	size_t strlen = cs.length();
	out.resize(strlen * 3 / 4 + 4);
	size_t decoded_len = 0;
	if (CryptoCore::b64_decode(out.ptrw(), out.size(), &decoded_len, (const uint8_t *)cs.get_data(), strlen) != OK) {
		r_err = ERR_INVALID_DATA;
		return PackedByteArray();
	}
	out.resize(decoded_len);
	return out;
}

Dictionary SandboxSerializer::_capture_envelope_v2(bool p_encode_blobs_as_base64) {
	// 1. Pre-Save Hydration Flush (§4.1): flush active Tier 2 hydrated actors into SoA memory.
	if (pre_save_flush_callback) {
		pre_save_flush_callback();
	}
	pre_save_flush_count++;

	Dictionary env;
	env["schema_version"] = SCHEMA_VERSION;
	env["engine_version"] = "4.7.2-sandbox";
	env["sim_tick_index"] = SandboxClock::get_singleton() ? SandboxClock::get_singleton()->get_sim_tick_index() : (uint64_t)0;
	env["sim_time_elapsed"] = SandboxClock::get_singleton() ? SandboxClock::get_singleton()->get_sim_time_elapsed() : 0.0;
	env["world_origin_64"] = WorldState::get_singleton() ? WorldState::get_singleton()->get_world_origin_64_array() : Array();
	env["world_state"] = WorldState::get_singleton() ? WorldState::get_singleton()->to_dictionary() : Dictionary();

	// Keep host-supplied blobs and data from optional modules across a save.
	// Live simulation modules update only the entries they own.
	Dictionary raw_sim = fallback_sim_modules.duplicate();
	if (sim_modules_write_callback) {
		sim_modules_write_callback(raw_sim);
	}
	Dictionary raw_stream = streaming_diffs_write_callback ? streaming_diffs_write_callback() : fallback_streaming_diffs;
	Dictionary raw_missions = missions_write_callback ? missions_write_callback() : fallback_missions;

	if (p_encode_blobs_as_base64) {
		Dictionary b64_sim;
		LocalVector<Variant> sim_keys = raw_sim.get_key_list();
		for (uint32_t i = 0; i < sim_keys.size(); i++) {
			PackedByteArray blob = raw_sim[sim_keys[i]];
			b64_sim[sim_keys[i].operator String()] = _bytes_to_b64(blob);
		}
		env["sim_modules"] = b64_sim;

		Dictionary b64_stream;
		LocalVector<Variant> stream_keys = raw_stream.get_key_list();
		for (uint32_t i = 0; i < stream_keys.size(); i++) {
			PackedByteArray blob = raw_stream[stream_keys[i]];
			b64_stream[stream_keys[i].operator String()] = _bytes_to_b64(blob);
		}
		env["streaming_cell_diffs"] = b64_stream;
	} else {
		env["sim_modules"] = raw_sim.duplicate(true);
		env["streaming_cell_diffs"] = raw_stream.duplicate(true);
	}

	env["missions"] = raw_missions.duplicate(true);
	return env;
}

Error SandboxSerializer::_apply_envelope_v2(const Dictionary &p_envelope, bool p_decode_blobs_from_base64) {
	if (_contains_forbidden_object_or_nan(p_envelope)) {
		return ERR_INVALID_DATA;
	}
	if (!p_envelope.has("schema_version")) {
		return ERR_INVALID_DATA;
	}
	int ver = (int)p_envelope["schema_version"];
	if (ver != SCHEMA_VERSION) {
		return ERR_INVALID_DATA;
	}

	if (p_envelope.has("world_origin_64")) {
		Variant orig_v = p_envelope["world_origin_64"];
		if (orig_v.get_type() != Variant::ARRAY) {
			return ERR_INVALID_DATA;
		}
		Array orig_arr = orig_v;
		if (orig_arr.size() != 3) {
			return ERR_INVALID_DATA;
		}
		if (WorldState::get_singleton()) {
			WorldState::get_singleton()->set_world_origin_64_array(orig_arr);
		}
	}

	if (p_envelope.has("world_state")) {
		if (p_envelope["world_state"].get_type() != Variant::DICTIONARY) {
			return ERR_INVALID_DATA;
		}
		if (WorldState::get_singleton()) {
			Error ws_err = WorldState::get_singleton()->from_dictionary(p_envelope["world_state"]);
			if (ws_err != OK) {
				return ws_err;
			}
		}
	}

	if (SandboxClock::get_singleton()) {
		uint64_t tick_idx = (uint64_t)(int64_t)p_envelope.get("sim_tick_index", 0);
		double elapsed = (double)p_envelope.get("sim_time_elapsed", 0.0);
		if (elapsed < 0.0 || Math::is_nan(elapsed) || Math::is_inf(elapsed)) {
			return ERR_INVALID_DATA;
		}
		SandboxClock::get_singleton()->set_sim_clock_state(tick_idx, elapsed);
	}

	if (p_envelope.has("sim_modules")) {
		if (p_envelope["sim_modules"].get_type() != Variant::DICTIONARY) {
			return ERR_INVALID_DATA;
		}
		Dictionary in_sim = p_envelope["sim_modules"];
		Dictionary decoded_sim;
		LocalVector<Variant> sim_keys = in_sim.get_key_list();
		for (uint32_t i = 0; i < sim_keys.size(); i++) {
			StringName k(sim_keys[i].operator String());
			const Variant &v = in_sim[sim_keys[i]];
			if (p_decode_blobs_from_base64 && v.get_type() == Variant::STRING) {
				Error b64_err = OK;
				PackedByteArray bytes = _b64_to_bytes(v.operator String(), b64_err);
				if (b64_err != OK) {
					return ERR_INVALID_DATA;
				}
				decoded_sim[k] = bytes;
			} else if (v.get_type() == Variant::PACKED_BYTE_ARRAY) {
				decoded_sim[k] = v;
			} else {
				return ERR_INVALID_DATA;
			}
		}
		fallback_sim_modules = decoded_sim;
		if (sim_modules_read_callback) {
			Error err = sim_modules_read_callback(decoded_sim);
			if (err != OK) {
				return err;
			}
		}
	}

	if (p_envelope.has("streaming_cell_diffs")) {
		if (p_envelope["streaming_cell_diffs"].get_type() != Variant::DICTIONARY) {
			return ERR_INVALID_DATA;
		}
		Dictionary in_stream = p_envelope["streaming_cell_diffs"];
		Dictionary decoded_stream;
		LocalVector<Variant> stream_keys = in_stream.get_key_list();
		for (uint32_t i = 0; i < stream_keys.size(); i++) {
			StringName k(stream_keys[i].operator String());
			const Variant &v = in_stream[stream_keys[i]];
			if (p_decode_blobs_from_base64 && v.get_type() == Variant::STRING) {
				Error b64_err = OK;
				PackedByteArray bytes = _b64_to_bytes(v.operator String(), b64_err);
				if (b64_err != OK) {
					return ERR_INVALID_DATA;
				}
				decoded_stream[k] = bytes;
			} else if (v.get_type() == Variant::PACKED_BYTE_ARRAY) {
				decoded_stream[k] = v;
			} else {
				return ERR_INVALID_DATA;
			}
		}
		fallback_streaming_diffs = decoded_stream;
		if (streaming_diffs_read_callback) {
			Error err = streaming_diffs_read_callback(decoded_stream);
			if (err != OK) {
				return err;
			}
		}
	}

	if (p_envelope.has("missions")) {
		if (p_envelope["missions"].get_type() != Variant::DICTIONARY) {
			return ERR_INVALID_DATA;
		}
		fallback_missions = p_envelope["missions"];
		if (missions_read_callback) {
			Error err = missions_read_callback(fallback_missions);
			if (err != OK) {
				return err;
			}
		}
	}

	if (post_load_resync_callback) {
		post_load_resync_callback();
	}
	if (EventBus::get_singleton()) {
		EventBus::get_singleton()->emit(SNAME("world_state_restored"), Dictionary());
	}

	return OK;
}

PackedByteArray SandboxSerializer::save_to_binary() {
	Dictionary env = _capture_envelope_v2(false);

	Ref<StreamPeerBuffer> spb;
	spb.instantiate();
	spb->put_data(SBSAV_MAGIC, sizeof(SBSAV_MAGIC));
	spb->put_u32(SCHEMA_VERSION);
	// Strictly p_full_objects = false so no Object instances can ever be written.
	spb->put_var(env, /*p_full_objects=*/false);
	return spb->get_data_array();
}

Error SandboxSerializer::load_from_binary(const PackedByteArray &p_data) {
	if (p_data.size() < 16) {
		return ERR_INVALID_DATA;
	}

	// Reject Godot native binary resource headers ("RSRC", "RSCC") or text resource headers ("[gd_").
	if ((p_data[0] == 'R' && p_data[1] == 'S' && (p_data[2] == 'R' || p_data[2] == 'C') && p_data[3] == 'C') ||
			(p_data[0] == '[' && p_data[1] == 'g' && p_data[2] == 'd' && p_data[3] == '_')) {
		return ERR_INVALID_DATA;
	}

	// Verify SBSAVv02 magic header.
	for (size_t i = 0; i < sizeof(SBSAV_MAGIC); i++) {
		if (p_data[i] != SBSAV_MAGIC[i]) {
			return ERR_INVALID_DATA;
		}
	}

	Ref<StreamPeerBuffer> spb;
	spb.instantiate();
	spb->set_data_array(p_data);
	spb->seek(sizeof(SBSAV_MAGIC));

	uint32_t ver = spb->get_u32();
	if (ver != SCHEMA_VERSION) {
		return ERR_INVALID_DATA;
	}

	// put_var() prefixes the encoded Variant with its byte count. Also accept
	// an unframed v2 dictionary, as the original reader did. Encoded Variant
	// lengths are four-byte aligned, so they cannot have the dictionary tag.
	int payload_offset = spb->get_position();
	uint32_t prefix = spb->get_u32();
	if ((prefix & 0xff) != Variant::DICTIONARY) {
		if (prefix != p_data.size() - spb->get_position()) {
			return ERR_INVALID_DATA;
		}
		payload_offset = spb->get_position();
	}
	const PackedByteArray payload_slice = p_data.slice(payload_offset);
	Variant decoded;
	Error err = deserialize_variant_safe(payload_slice, decoded);
	if (err != OK || decoded.get_type() != Variant::DICTIONARY) {
		return ERR_INVALID_DATA;
	}

	return _apply_envelope_v2(decoded, false);
}

String SandboxSerializer::save_to_json() {
	Dictionary env = _capture_envelope_v2(true);
	return JSON::stringify(env, "  ", true, true);
}

Error SandboxSerializer::load_from_json(const String &p_json) {
	if (p_json.is_empty()) {
		return ERR_INVALID_DATA;
	}
	// Reject text-based Godot resource files (.tres / .scn) or embedded Object syntax.
	if (p_json.begins_with("[gd_resource") || p_json.begins_with("[gd_scene") || p_json.contains("Object(") || p_json.contains("GDScript")) {
		return ERR_INVALID_DATA;
	}

	Ref<JSON> json;
	json.instantiate();
	if (json->parse(p_json) != OK) {
		return ERR_INVALID_DATA;
	}

	Variant data = json->get_data();
	if (data.get_type() != Variant::DICTIONARY) {
		return ERR_INVALID_DATA;
	}
	return _apply_envelope_v2(data, true);
}

Error SandboxSerializer::save_file(const String &p_path) {
	String lower = p_path.to_lower();
	// Strictly ban ResourceSaver formats (.tres, .res, .scn) for player save files (§4).
	if (lower.ends_with(".tres") || lower.ends_with(".res") || lower.ends_with(".scn")) {
		return ERR_INVALID_DATA;
	}

	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::WRITE);
	if (f.is_null()) {
		return ERR_CANT_OPEN;
	}

	if (lower.ends_with(".json")) {
		String j = save_to_json();
		f->store_string(j);
		return OK;
	} else if (lower.ends_with(".sbsav")) {
		PackedByteArray bin = save_to_binary();
		f->store_buffer(bin.ptr(), bin.size());
		return OK;
	}
	return ERR_INVALID_DATA;
}

Error SandboxSerializer::load_file(const String &p_path) {
	String lower = p_path.to_lower();
	if (lower.ends_with(".tres") || lower.ends_with(".res") || lower.ends_with(".scn")) {
		return ERR_INVALID_DATA;
	}

	if (!FileAccess::exists(p_path)) {
		return ERR_FILE_NOT_FOUND;
	}

	if (lower.ends_with(".json")) {
		Error err = OK;
		String content = FileAccess::get_file_as_string(p_path, &err);
		if (err != OK) {
			return err;
		}
		return load_from_json(content);
	} else if (lower.ends_with(".sbsav")) {
		Error err = OK;
		Vector<uint8_t> bytes = FileAccess::get_file_as_bytes(p_path, &err);
		if (err != OK) {
			return err;
		}
		return load_from_binary(bytes);
	}
	return ERR_INVALID_DATA;
}

void SandboxSerializer::set_sim_module_blob(const StringName &p_module_id, const PackedByteArray &p_blob) {
	fallback_sim_modules[p_module_id] = p_blob;
}

PackedByteArray SandboxSerializer::get_sim_module_blob(const StringName &p_module_id) const {
	if (fallback_sim_modules.has(p_module_id)) {
		return fallback_sim_modules[p_module_id];
	}
	return PackedByteArray();
}

void SandboxSerializer::set_streaming_cell_diff(const StringName &p_cell_id, const PackedByteArray &p_diff) {
	fallback_streaming_diffs[p_cell_id] = p_diff;
}

PackedByteArray SandboxSerializer::get_streaming_cell_diff(const StringName &p_cell_id) const {
	if (fallback_streaming_diffs.has(p_cell_id)) {
		return fallback_streaming_diffs[p_cell_id];
	}
	return PackedByteArray();
}

void SandboxSerializer::set_missions_snapshot(const Dictionary &p_missions) {
	fallback_missions = p_missions.duplicate(true);
}

Dictionary SandboxSerializer::get_missions_snapshot() const {
	return fallback_missions;
}

void SandboxSerializer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("save_to_binary"), &SandboxSerializer::save_to_binary);
	ClassDB::bind_method(D_METHOD("load_from_binary", "data"), &SandboxSerializer::load_from_binary);
	ClassDB::bind_method(D_METHOD("save_to_json"), &SandboxSerializer::save_to_json);
	ClassDB::bind_method(D_METHOD("load_from_json", "json"), &SandboxSerializer::load_from_json);
	ClassDB::bind_method(D_METHOD("save_file", "path"), &SandboxSerializer::save_file);
	ClassDB::bind_method(D_METHOD("load_file", "path"), &SandboxSerializer::load_file);
	ClassDB::bind_method(D_METHOD("set_sim_module_blob", "module_id", "blob"), &SandboxSerializer::set_sim_module_blob);
	ClassDB::bind_method(D_METHOD("get_sim_module_blob", "module_id"), &SandboxSerializer::get_sim_module_blob);
	ClassDB::bind_method(D_METHOD("set_streaming_cell_diff", "cell_id", "diff"), &SandboxSerializer::set_streaming_cell_diff);
	ClassDB::bind_method(D_METHOD("get_streaming_cell_diff", "cell_id"), &SandboxSerializer::get_streaming_cell_diff);
	ClassDB::bind_method(D_METHOD("set_missions_snapshot", "missions"), &SandboxSerializer::set_missions_snapshot);
	ClassDB::bind_method(D_METHOD("get_missions_snapshot"), &SandboxSerializer::get_missions_snapshot);
	ClassDB::bind_method(D_METHOD("get_pre_save_flush_count"), &SandboxSerializer::get_pre_save_flush_count);
}

SandboxSerializer::SandboxSerializer() {
	singleton = this;
	fallback_missions["active"] = Array();
	fallback_missions["completed"] = Array();
	fallback_missions["objective_progress"] = Dictionary();
}

SandboxSerializer::~SandboxSerializer() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
