/**************************************************************************/
/*  sandbox_serializer.h                                                  */
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

#include "core/object/class_db.h"
#include "core/object/object.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

class SandboxSerializer : public Object {
	GDCLASS(SandboxSerializer, Object);

public:
	static constexpr int SCHEMA_VERSION = 2;

	typedef void (*PreSaveFlushCallback)();
	typedef void (*PostLoadResyncCallback)();
	typedef Dictionary (*SectionWriteCallback)();
	typedef void (*SimModulesWriteCallback)(Dictionary &r_modules);
	typedef Error (*SectionReadCallback)(const Dictionary &p_section);

private:
	static inline SandboxSerializer *singleton = nullptr;

	PreSaveFlushCallback pre_save_flush_callback = nullptr;
	PostLoadResyncCallback post_load_resync_callback = nullptr;

	SimModulesWriteCallback sim_modules_write_callback = nullptr;
	SectionReadCallback sim_modules_read_callback = nullptr;

	SectionWriteCallback streaming_diffs_write_callback = nullptr;
	SectionReadCallback streaming_diffs_read_callback = nullptr;

	SectionWriteCallback missions_write_callback = nullptr;
	SectionReadCallback missions_read_callback = nullptr;

	// Fallback sections when optional subsystems are not registered
	Dictionary fallback_sim_modules;
	Dictionary fallback_streaming_diffs;
	Dictionary fallback_missions;

	uint32_t pre_save_flush_count = 0;

	static bool _contains_forbidden_object_or_nan(const Variant &p_var, int p_depth = 0);
	static PackedByteArray _b64_to_bytes(const String &p_b64, Error &r_err);
	static String _bytes_to_b64(const PackedByteArray &p_bytes);

	Dictionary _capture_envelope_v2(bool p_encode_blobs_as_base64);
	Error _apply_envelope_v2(const Dictionary &p_envelope, bool p_decode_blobs_from_base64);

protected:
	static void _bind_methods();

public:
	static SandboxSerializer *get_singleton() { return singleton; }

	// Hardened Deserializer Guardrail (§4.1 & §5): enforces allow_objects = false and rejects EncodedObjectAsID / scripts.
	static Error deserialize_variant_safe(const PackedByteArray &p_bytes, Variant &r_out);

	// Binary Snapshot (.sbsav) API via StreamPeerBuffer
	PackedByteArray save_to_binary();
	Error load_from_binary(const PackedByteArray &p_data);

	// Structured JSON (.json) API
	String save_to_json();
	Error load_from_json(const String &p_json);

	// File Persistence (Bans .tres / .res / .scn ResourceSaver formats)
	Error save_file(const String &p_path);
	Error load_file(const String &p_path);

	// Section fallback accessors for unit tests & standalone usage
	void set_sim_module_blob(const StringName &p_module_id, const PackedByteArray &p_blob);
	PackedByteArray get_sim_module_blob(const StringName &p_module_id) const;

	void set_streaming_cell_diff(const StringName &p_cell_id, const PackedByteArray &p_diff);
	PackedByteArray get_streaming_cell_diff(const StringName &p_cell_id) const;

	void set_missions_snapshot(const Dictionary &p_missions);
	Dictionary get_missions_snapshot() const;

	uint32_t get_pre_save_flush_count() const { return pre_save_flush_count; }

	// Hooks registered by Tier 2 modules (SimulationServer, StreamingServer, HydrationBubble, MissionRunner)
	void set_pre_save_flush_callback(PreSaveFlushCallback p_cb) { pre_save_flush_callback = p_cb; }
	void set_post_load_resync_callback(PostLoadResyncCallback p_cb) { post_load_resync_callback = p_cb; }
	void set_sim_modules_callbacks(SimModulesWriteCallback p_write, SectionReadCallback p_read) {
		sim_modules_write_callback = p_write;
		sim_modules_read_callback = p_read;
	}
	void set_streaming_diffs_callbacks(SectionWriteCallback p_write, SectionReadCallback p_read) {
		streaming_diffs_write_callback = p_write;
		streaming_diffs_read_callback = p_read;
	}
	void set_missions_callbacks(SectionWriteCallback p_write, SectionReadCallback p_read) {
		missions_write_callback = p_write;
		missions_read_callback = p_read;
	}

	SandboxSerializer();
	~SandboxSerializer();
};
