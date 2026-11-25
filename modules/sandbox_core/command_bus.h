/**************************************************************************/
/*  command_bus.h                                                         */
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
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/templates/vector.h"
#include "core/variant/callable.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"

class CommandBus : public Object {
	GDCLASS(CommandBus, Object);

public:
	enum PermissionLevel {
		PERMISSION_GUEST = 0,
		PERMISSION_USER = 1,
		PERMISSION_ADMIN = 2,
		PERMISSION_DEV = 3,
	};

	struct CommandSchema {
		StringName name;
		PermissionLevel min_permission = PERMISSION_USER;
		Vector<Variant::Type> arg_signature;
		Callable handler;
		String help_text;
	};

	struct AuditRecord {
		uint64_t timestamp_usec = 0;
		StringName caller_id;
		PermissionLevel permission_level = PERMISSION_GUEST;
		String command_string;
		bool ok = false;
		Error error_code = OK;
	};

	static constexpr int MAX_COMMAND_BUFFER_BYTES = 512;

private:
	static inline CommandBus *singleton = nullptr;

	HashMap<StringName, CommandSchema> commands;
	LocalVector<AuditRecord> audit_log;
	uint32_t max_audit_entries = 256;

	int rate_limit_per_sec = 30;
	HashMap<StringName, LocalVector<uint64_t>> caller_execution_timestamps;

	void _record_audit(const StringName &p_caller_id, PermissionLevel p_permission, const String &p_command_str, bool p_ok, Error p_err);
	bool _check_rate_limit(const StringName &p_caller_id, uint64_t p_now_usec);
	static Dictionary _make_result(bool p_ok, const String &p_output, Error p_err);
	static bool _coerce_argument(const Variant &p_in, Variant::Type p_target_type, Variant &r_out);

	// Built-in core command handlers
	Variant _cmd_help(const Array &p_args, PermissionLevel p_permission, const TypedArray<StringName> &p_allowed_namespaces);
	Variant _cmd_sim_pause();
	Variant _cmd_sim_resume();
	Variant _cmd_sim_speed(double p_scale);
	Variant _cmd_sim_step(int64_t p_steps);
	Variant _cmd_state_get(const StringName &p_key);
	Variant _cmd_state_set(const StringName &p_key, const String &p_raw_val);
	Variant _cmd_event_fire(const StringName &p_event_id);
#ifdef DEBUG_ENABLED
	Variant _cmd_dev_inspect();
#endif

protected:
	static void _bind_methods();

public:
	static CommandBus *get_singleton() { return singleton; }

	void register_builtin_commands();

	void register_command(const StringName &p_name, PermissionLevel p_min_permission, const TypedArray<int> &p_arg_signature, const Callable &p_handler, const String &p_help_text = String());
	void register_command_native(const StringName &p_name, PermissionLevel p_min_permission, const Vector<Variant::Type> &p_arg_signature, const Callable &p_handler, const String &p_help_text = String());
	void unregister_command(const StringName &p_name);
	bool has_command(const StringName &p_name) const;

	Dictionary execute(const String &p_command_line, PermissionLevel p_caller_permission = PERMISSION_USER, const StringName &p_caller_id = SNAME("local"), const TypedArray<StringName> &p_allowed_namespaces = TypedArray<StringName>());
	Dictionary execute_args(const StringName &p_command_name, const Array &p_args, PermissionLevel p_caller_permission = PERMISSION_USER, const StringName &p_caller_id = SNAME("local"), const TypedArray<StringName> &p_allowed_namespaces = TypedArray<StringName>());

	PackedStringArray get_autocomplete_suggestions(const String &p_prefix, PermissionLevel p_caller_permission = PERMISSION_USER, const TypedArray<StringName> &p_allowed_namespaces = TypedArray<StringName>()) const;
	Dictionary get_command_info(const StringName &p_name) const;
	PackedStringArray get_registered_commands() const;

	void set_rate_limit_per_sec(int p_limit);
	int get_rate_limit_per_sec() const { return rate_limit_per_sec; }
	void clear_rate_limit_state();

	TypedArray<Dictionary> get_audit_log() const;
	void clear_audit_log();

	CommandBus();
	~CommandBus();
};

VARIANT_ENUM_CAST(CommandBus::PermissionLevel);
