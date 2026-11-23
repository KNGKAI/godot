/**************************************************************************/
/*  command_bus.cpp                                                       */
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

#include "command_bus.h"

#include "event_bus.h"
#include "sandbox_clock.h"
#include "world_state.h"

#include "core/object/callable_mp.h"
#include "core/os/os.h"

Dictionary CommandBus::_make_result(bool p_ok, const String &p_output, Error p_err) {
	Dictionary d;
	d["ok"] = p_ok;
	d["output"] = p_output;
	d["error_code"] = (int)p_err;
	return d;
}

void CommandBus::_record_audit(const StringName &p_caller_id, PermissionLevel p_permission, const String &p_command_str, bool p_ok, Error p_err) {
	AuditRecord rec;
	rec.timestamp_usec = OS::get_singleton() ? OS::get_singleton()->get_ticks_usec() : 0;
	rec.caller_id = p_caller_id;
	rec.permission_level = p_permission;
	rec.command_string = p_command_str;
	rec.ok = p_ok;
	rec.error_code = p_err;

	if (audit_log.size() >= max_audit_entries) {
		audit_log.remove_at(0);
	}
	audit_log.push_back(rec);
}

bool CommandBus::_check_rate_limit(const StringName &p_caller_id, uint64_t p_now_usec) {
	if (rate_limit_per_sec <= 0) {
		return true;
	}
	LocalVector<uint64_t> &timestamps = caller_execution_timestamps[p_caller_id];
	const uint64_t window_usec = 1000000ULL;
	uint32_t idx = 0;
	while (idx < timestamps.size()) {
		if (p_now_usec >= timestamps[idx] && (p_now_usec - timestamps[idx]) >= window_usec) {
			timestamps.remove_at(idx);
		} else {
			idx++;
		}
	}
	if ((int)timestamps.size() >= rate_limit_per_sec) {
		return false;
	}
	timestamps.push_back(p_now_usec);
	return true;
}

namespace {

struct CommandToken {
	String text;
	bool quoted = false;
};

bool tokenize_command_line(const String &p_line, Vector<CommandToken> &r_tokens) {
	CommandToken current;
	char32_t quote_char = 0;

	for (int i = 0; i < p_line.length(); i++) {
		char32_t c = p_line[i];
		if (quote_char) {
			if (c == quote_char) {
				quote_char = 0;
			} else if (c == '\\' && i + 1 < p_line.length()) {
				current.text += p_line[++i];
			} else {
				current.text += c;
			}
		} else if (c == '"' || c == '\'') {
			current.quoted = true;
			quote_char = c;
		} else if (c == ' ' || c == '\t') {
			if (current.quoted || !current.text.is_empty()) {
				r_tokens.push_back(current);
				current = CommandToken();
			}
		} else {
			current.text += c;
		}
	}
	if (quote_char) {
		return false;
	}
	if (current.quoted || !current.text.is_empty()) {
		r_tokens.push_back(current);
	}
	return true;
}

Variant parse_scalar_literal(const String &p_text) {
	if (p_text == "null") {
		return Variant();
	}
	if (p_text == "true" || p_text == "false") {
		return p_text == "true";
	}
	if (p_text.is_valid_int()) {
		return p_text.to_int();
	}
	if (p_text.is_valid_float()) {
		return p_text.to_float();
	}
	return p_text;
}

bool is_available_permission(CommandBus::PermissionLevel p_permission) {
#ifdef DEBUG_ENABLED
	constexpr CommandBus::PermissionLevel highest_tier = CommandBus::PERMISSION_DEV;
#else
	constexpr CommandBus::PermissionLevel highest_tier = CommandBus::PERMISSION_ADMIN;
#endif
	return p_permission >= CommandBus::PERMISSION_GUEST && p_permission <= highest_tier;
}

bool is_namespace_allowed(const StringName &p_name, const TypedArray<StringName> &p_allowed_namespaces) {
	if (p_allowed_namespaces.is_empty() || p_name == SNAME("help")) {
		return true;
	}
	const String name = p_name;
	return p_allowed_namespaces.has(StringName(name.get_slice(".", 0)));
}

} // namespace

bool CommandBus::_coerce_argument(const Variant &p_in, Variant::Type p_target_type, Variant &r_out) {
	if (p_target_type == Variant::NIL) {
		r_out = p_in;
		return true;
	}
	if (p_in.get_type() == p_target_type) {
		r_out = p_in;
		return true;
	}
	if (p_in.get_type() == Variant::STRING || p_in.get_type() == Variant::STRING_NAME) {
		String s = p_in.operator String().strip_edges();
		switch (p_target_type) {
			case Variant::STRING:
				r_out = s;
				return true;
			case Variant::STRING_NAME:
				r_out = StringName(s);
				return true;
			case Variant::BOOL: {
				String lower = s.to_lower();
				if (lower == "true" || lower == "1" || lower == "yes" || lower == "on") {
					r_out = true;
					return true;
				}
				if (lower == "false" || lower == "0" || lower == "no" || lower == "off") {
					r_out = false;
					return true;
				}
				return false;
			}
			case Variant::INT:
				if (s.is_valid_int()) {
					r_out = s.to_int();
					return true;
				}
				return false;
			case Variant::FLOAT:
				if (s.is_valid_float()) {
					r_out = s.to_float();
					return true;
				}
				return false;
			default:
				return false;
		}
	}
	if (p_target_type == Variant::FLOAT && p_in.get_type() == Variant::INT) {
		r_out = (double)p_in.operator int64_t();
		return true;
	}
	if (p_target_type == Variant::STRING_NAME && p_in.get_type() == Variant::STRING) {
		r_out = StringName(p_in.operator String());
		return true;
	}
	if (p_target_type == Variant::STRING && p_in.get_type() == Variant::STRING_NAME) {
		r_out = p_in.operator String();
		return true;
	}
	return false;
}

void CommandBus::register_command(const StringName &p_name, PermissionLevel p_min_permission, const TypedArray<int> &p_arg_signature, const Callable &p_handler, const String &p_help_text) {
	Vector<Variant::Type> sig;
	for (int i = 0; i < p_arg_signature.size(); i++) {
		sig.push_back((Variant::Type)(int)p_arg_signature[i]);
	}
	register_command_native(p_name, p_min_permission, sig, p_handler, p_help_text);
}

void CommandBus::register_command_native(const StringName &p_name, PermissionLevel p_min_permission, const Vector<Variant::Type> &p_arg_signature, const Callable &p_handler, const String &p_help_text) {
	ERR_FAIL_COND(p_name == StringName());
	ERR_FAIL_COND(!p_handler.is_valid());

	if (!is_available_permission(p_min_permission)) {
		return;
	}

	CommandSchema schema;
	schema.name = p_name;
	schema.min_permission = p_min_permission;
	schema.arg_signature = p_arg_signature;
	schema.handler = p_handler;
	schema.help_text = p_help_text;
	commands[p_name] = schema;
}

void CommandBus::unregister_command(const StringName &p_name) {
	commands.erase(p_name);
}

bool CommandBus::has_command(const StringName &p_name) const {
	return commands.has(p_name);
}

Dictionary CommandBus::execute(const String &p_command_line, PermissionLevel p_caller_permission, const StringName &p_caller_id, const TypedArray<StringName> &p_allowed_namespaces) {
	// Security §5: 512-byte buffer cap against terminal DoS / injection.
	if (p_command_line.length() > MAX_COMMAND_BUFFER_BYTES || p_command_line.utf8().length() > MAX_COMMAND_BUFFER_BYTES) {
		_record_audit(p_caller_id, p_caller_permission, p_command_line.substr(0, 64), false, ERR_INVALID_DATA);
		return _make_result(false, "Command exceeds 512-byte maximum length.", ERR_INVALID_DATA);
	}

	String trimmed = p_command_line.strip_edges();
	if (trimmed.is_empty()) {
		return _make_result(false, "Empty command.", ERR_INVALID_PARAMETER);
	}

	Vector<CommandToken> tokens;
	if (!tokenize_command_line(trimmed, tokens)) {
		_record_audit(p_caller_id, p_caller_permission, trimmed, false, ERR_PARSE_ERROR);
		return _make_result(false, "Unterminated quoted argument.", ERR_PARSE_ERROR);
	}
	if (tokens.is_empty()) {
		return _make_result(false, "Empty command.", ERR_INVALID_PARAMETER);
	}

	StringName cmd_name(tokens[0].text);
	const CommandSchema *schema = commands.getptr(cmd_name);
	Array raw_args;
	for (int i = 1; i < tokens.size(); i++) {
		// Only wildcard arguments need literal inference. Preserve the exact spelling
		// of strings and let schema validation handle all declared argument types.
		if (schema && i - 1 < schema->arg_signature.size() && schema->arg_signature[i - 1] == Variant::NIL && !tokens[i].quoted) {
			raw_args.push_back(parse_scalar_literal(tokens[i].text));
		} else {
			raw_args.push_back(tokens[i].text);
		}
	}

	return execute_args(cmd_name, raw_args, p_caller_permission, p_caller_id, p_allowed_namespaces);
}

Dictionary CommandBus::execute_args(const StringName &p_command_name, const Array &p_args, PermissionLevel p_caller_permission, const StringName &p_caller_id, const TypedArray<StringName> &p_allowed_namespaces) {
	String audit_cmd = String(p_command_name);
	for (int i = 0; i < p_args.size(); i++) {
		audit_cmd += " " + p_args[i].operator String();
	}

	if (!is_available_permission(p_caller_permission)) {
		_record_audit(p_caller_id, p_caller_permission, audit_cmd, false, ERR_UNAUTHORIZED);
		return _make_result(false, "Permission tier is unavailable.", ERR_UNAUTHORIZED);
	}

	uint64_t now_usec = OS::get_singleton() ? OS::get_singleton()->get_ticks_usec() : 0;
	if (!_check_rate_limit(p_caller_id, now_usec)) {
		_record_audit(p_caller_id, p_caller_permission, audit_cmd, false, ERR_BUSY);
		return _make_result(false, "Command rate limit exceeded.", ERR_BUSY);
	}

	// Check namespace whitelist if specified on the terminal instance.
	if (!is_namespace_allowed(p_command_name, p_allowed_namespaces)) {
		_record_audit(p_caller_id, p_caller_permission, audit_cmd, false, ERR_UNAUTHORIZED);
		return _make_result(false, vformat("Namespace '%s' is not permitted on this terminal.", String(p_command_name).get_slice(".", 0)), ERR_UNAUTHORIZED);
	}

	HashMap<StringName, CommandSchema>::ConstIterator it = commands.find(p_command_name);
	if (!it) {
		_record_audit(p_caller_id, p_caller_permission, audit_cmd, false, ERR_UNAUTHORIZED);
		return _make_result(false, vformat("Unknown command: '%s'.", String(p_command_name)), ERR_UNAUTHORIZED);
	}

	const CommandSchema &schema = it->value;
	if (p_caller_permission < schema.min_permission) {
		_record_audit(p_caller_id, p_caller_permission, audit_cmd, false, ERR_UNAUTHORIZED);
		return _make_result(false, vformat("Insufficient permissions for '%s'.", String(p_command_name)), ERR_UNAUTHORIZED);
	}

	// Validate & coerce arguments against schema.arg_signature.
	Vector<Variant> validated_args;
	if (p_command_name != SNAME("help")) {
		if (p_args.size() != schema.arg_signature.size()) {
			_record_audit(p_caller_id, p_caller_permission, audit_cmd, false, ERR_INVALID_PARAMETER);
			return _make_result(false, vformat("Command '%s' expects %d argument(s), got %d.", String(p_command_name), (int)schema.arg_signature.size(), p_args.size()), ERR_INVALID_PARAMETER);
		}
		for (int i = 0; i < schema.arg_signature.size(); i++) {
			Variant coerced;
			if (!_coerce_argument(p_args[i], schema.arg_signature[i], coerced)) {
				_record_audit(p_caller_id, p_caller_permission, audit_cmd, false, ERR_INVALID_PARAMETER);
				return _make_result(false, vformat("Invalid type for argument %d of '%s' (expected %s).", i + 1, String(p_command_name), Variant::get_type_name(schema.arg_signature[i])), ERR_INVALID_PARAMETER);
			}
			validated_args.push_back(coerced);
		}
	}

	Variant ret;
	Callable::CallError ce;
	if (p_command_name == SNAME("help")) {
		ret = _cmd_help(p_args, p_caller_permission, p_allowed_namespaces);
		ce.error = Callable::CallError::CALL_OK;
	} else {
		LocalVector<const Variant *> arg_ptrs;
		for (int i = 0; i < validated_args.size(); i++) {
			arg_ptrs.push_back(&validated_args[i]);
		}
		schema.handler.callp(arg_ptrs.is_empty() ? nullptr : arg_ptrs.ptr(), validated_args.size(), ret, ce);
	}

	if (ce.error != Callable::CallError::CALL_OK) {
		_record_audit(p_caller_id, p_caller_permission, audit_cmd, false, ERR_CANT_ACQUIRE_RESOURCE);
		return _make_result(false, vformat("Execution failed for '%s'.", String(p_command_name)), ERR_CANT_ACQUIRE_RESOURCE);
	}

	if (ret.get_type() == Variant::DICTIONARY) {
		Dictionary d = ret;
		if (d.has("ok") && d.has("output") && d.has("error_code")) {
			bool ok = d["ok"];
			Error err = (Error)(int)d["error_code"];
			_record_audit(p_caller_id, p_caller_permission, audit_cmd, ok, err);
			return d;
		}
	}

	String out_str = (ret.get_type() == Variant::NIL) ? String("OK") : ret.operator String();
	_record_audit(p_caller_id, p_caller_permission, audit_cmd, true, OK);
	return _make_result(true, out_str, OK);
}

PackedStringArray CommandBus::get_autocomplete_suggestions(const String &p_prefix, PermissionLevel p_caller_permission, const TypedArray<StringName> &p_allowed_namespaces) const {
	PackedStringArray matches;
	if (!is_available_permission(p_caller_permission)) {
		return matches;
	}
	for (const KeyValue<StringName, CommandSchema> &E : commands) {
		if (p_caller_permission < E.value.min_permission) {
			continue;
		}
		String name_str = String(E.key);
		if (!p_prefix.is_empty() && !name_str.begins_with(p_prefix)) {
			continue;
		}
		if (!is_namespace_allowed(E.key, p_allowed_namespaces)) {
			continue;
		}
		matches.push_back(name_str);
	}
	matches.sort();
	return matches;
}

Dictionary CommandBus::get_command_info(const StringName &p_name) const {
	Dictionary d;
	HashMap<StringName, CommandSchema>::ConstIterator it = commands.find(p_name);
	if (!it) {
		return d;
	}
	d["name"] = it->value.name;
	d["min_permission"] = (int)it->value.min_permission;
	d["help_text"] = it->value.help_text;
	TypedArray<int> sig;
	for (int i = 0; i < it->value.arg_signature.size(); i++) {
		sig.push_back((int)it->value.arg_signature[i]);
	}
	d["arg_signature"] = sig;
	return d;
}

PackedStringArray CommandBus::get_registered_commands() const {
	PackedStringArray list;
	for (const KeyValue<StringName, CommandSchema> &E : commands) {
		list.push_back(String(E.key));
	}
	list.sort();
	return list;
}

void CommandBus::set_rate_limit_per_sec(int p_limit) {
	rate_limit_per_sec = p_limit;
}

void CommandBus::clear_rate_limit_state() {
	caller_execution_timestamps.clear();
}

TypedArray<Dictionary> CommandBus::get_audit_log() const {
	TypedArray<Dictionary> out;
	for (uint32_t i = 0; i < audit_log.size(); i++) {
		Dictionary d;
		d["timestamp_usec"] = audit_log[i].timestamp_usec;
		d["caller_id"] = audit_log[i].caller_id;
		d["permission_level"] = (int)audit_log[i].permission_level;
		d["command_string"] = audit_log[i].command_string;
		d["ok"] = audit_log[i].ok;
		d["error_code"] = (int)audit_log[i].error_code;
		out.push_back(d);
	}
	return out;
}

void CommandBus::clear_audit_log() {
	audit_log.clear();
}

Variant CommandBus::_cmd_help(const Array &p_args, PermissionLevel p_permission, const TypedArray<StringName> &p_allowed_namespaces) {
	String prefix = p_args.is_empty() ? String() : p_args[0].operator String();
	PackedStringArray names = get_autocomplete_suggestions(prefix, p_permission, p_allowed_namespaces);
	String out;
	for (int i = 0; i < names.size(); i++) {
		const CommandSchema &s = commands[StringName(names[i])];
		if (!out.is_empty()) {
			out += "\n";
		}
		out += names[i];
		if (!s.help_text.is_empty()) {
			out += " - " + s.help_text;
		}
	}
	return out;
}

Variant CommandBus::_cmd_sim_pause() {
	if (SandboxClock::get_singleton()) {
		SandboxClock::get_singleton()->set_sim_paused(true);
	}
	return "Simulation paused.";
}

Variant CommandBus::_cmd_sim_resume() {
	if (SandboxClock::get_singleton()) {
		SandboxClock::get_singleton()->set_sim_paused(false);
	}
	return "Simulation resumed.";
}

Variant CommandBus::_cmd_sim_speed(double p_scale) {
	if (SandboxClock::get_singleton()) {
		SandboxClock::get_singleton()->set_sim_time_scale(p_scale);
		return vformat("Simulation speed set to %.2fx.", SandboxClock::get_singleton()->get_sim_time_scale());
	}
	return "SandboxClock unavailable.";
}

Variant CommandBus::_cmd_sim_step(int64_t p_steps) {
	if (SandboxClock::get_singleton()) {
		SandboxClock::get_singleton()->step((int)MAX((int64_t)1, p_steps));
		return vformat("Queued %d simulation step(s).", (int)MAX((int64_t)1, p_steps));
	}
	return "SandboxClock unavailable.";
}

Variant CommandBus::_cmd_state_get(const StringName &p_key) {
	if (WorldState::get_singleton()) {
		if (!WorldState::get_singleton()->has_key(p_key)) {
			return _make_result(false, vformat("WorldState key '%s' not found.", String(p_key)), ERR_DOES_NOT_EXIST);
		}
		return WorldState::get_singleton()->get_value(p_key).operator String();
	}
	return _make_result(false, "WorldState unavailable.", ERR_UNAVAILABLE);
}

Variant CommandBus::_cmd_state_set(const StringName &p_key, const String &p_raw_val) {
	if (!WorldState::get_singleton()) {
		return _make_result(false, "WorldState unavailable.", ERR_UNAVAILABLE);
	}
	String s = p_raw_val.strip_edges();
	Variant parsed = s;
	if (s.to_lower() == "true") {
		parsed = true;
	} else if (s.to_lower() == "false") {
		parsed = false;
	} else if (s.is_valid_int()) {
		parsed = s.to_int();
	} else if (s.is_valid_float()) {
		parsed = s.to_float();
	}
	WorldState::get_singleton()->set_value(p_key, parsed);
	return vformat("%s = %s", String(p_key), parsed.operator String());
}

Variant CommandBus::_cmd_event_fire(const StringName &p_event_id) {
	if (EventBus::get_singleton()) {
		EventBus::get_singleton()->emit(p_event_id, Dictionary());
		return vformat("Event '%s' emitted.", String(p_event_id));
	}
	return _make_result(false, "EventBus unavailable.", ERR_UNAVAILABLE);
}

#ifdef DEBUG_ENABLED
Variant CommandBus::_cmd_dev_inspect() {
	return "DEV diagnostic mode active.";
}
#endif

void CommandBus::register_builtin_commands() {
	register_command_native(SNAME("help"), PERMISSION_GUEST, Vector<Variant::Type>(), callable_mp(this, &CommandBus::_cmd_help), "List available commands.");
	register_command_native(SNAME("sim.pause"), PERMISSION_ADMIN, Vector<Variant::Type>(), callable_mp(this, &CommandBus::_cmd_sim_pause), "Pause sim_clock.");
	register_command_native(SNAME("sim.resume"), PERMISSION_ADMIN, Vector<Variant::Type>(), callable_mp(this, &CommandBus::_cmd_sim_resume), "Resume sim_clock.");

	Vector<Variant::Type> sig_float;
	sig_float.push_back(Variant::FLOAT);
	register_command_native(SNAME("sim.speed"), PERMISSION_ADMIN, sig_float, callable_mp(this, &CommandBus::_cmd_sim_speed), "Set sim_clock speed multiplier (0.0 - 100.0).");

	Vector<Variant::Type> sig_int;
	sig_int.push_back(Variant::INT);
	register_command_native(SNAME("sim.step"), PERMISSION_ADMIN, sig_int, callable_mp(this, &CommandBus::_cmd_sim_step), "Advance sim_clock by N fixed ticks.");

	Vector<Variant::Type> sig_key;
	sig_key.push_back(Variant::STRING_NAME);
	register_command_native(SNAME("state.get"), PERMISSION_USER, sig_key, callable_mp(this, &CommandBus::_cmd_state_get), "Read a WorldState variable.");

	Vector<Variant::Type> sig_key_val;
	sig_key_val.push_back(Variant::STRING_NAME);
	sig_key_val.push_back(Variant::STRING);
	register_command_native(SNAME("state.set"), PERMISSION_ADMIN, sig_key_val, callable_mp(this, &CommandBus::_cmd_state_set), "Set a WorldState variable.");

	register_command_native(SNAME("event.fire"), PERMISSION_ADMIN, sig_key, callable_mp(this, &CommandBus::_cmd_event_fire), "Emit an EventBus signal.");

#ifdef DEBUG_ENABLED
	register_command_native(SNAME("dev.inspect"), PERMISSION_DEV, Vector<Variant::Type>(), callable_mp(this, &CommandBus::_cmd_dev_inspect), "Developer-only inspection command.");
#endif
}

void CommandBus::_bind_methods() {
	ClassDB::bind_method(D_METHOD("register_command", "name", "min_permission", "arg_signature", "handler", "help_text"), &CommandBus::register_command, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("unregister_command", "name"), &CommandBus::unregister_command);
	ClassDB::bind_method(D_METHOD("has_command", "name"), &CommandBus::has_command);
	ClassDB::bind_method(D_METHOD("execute", "command_line", "caller_permission", "caller_id", "allowed_namespaces"), &CommandBus::execute, DEFVAL(PERMISSION_USER), DEFVAL(SNAME("local")), DEFVAL(TypedArray<StringName>()));
	ClassDB::bind_method(D_METHOD("execute_args", "command_name", "args", "caller_permission", "caller_id", "allowed_namespaces"), &CommandBus::execute_args, DEFVAL(PERMISSION_USER), DEFVAL(SNAME("local")), DEFVAL(TypedArray<StringName>()));
	ClassDB::bind_method(D_METHOD("get_autocomplete_suggestions", "prefix", "caller_permission", "allowed_namespaces"), &CommandBus::get_autocomplete_suggestions, DEFVAL(PERMISSION_USER), DEFVAL(TypedArray<StringName>()));
	ClassDB::bind_method(D_METHOD("get_command_info", "name"), &CommandBus::get_command_info);
	ClassDB::bind_method(D_METHOD("get_registered_commands"), &CommandBus::get_registered_commands);
	ClassDB::bind_method(D_METHOD("set_rate_limit_per_sec", "limit"), &CommandBus::set_rate_limit_per_sec);
	ClassDB::bind_method(D_METHOD("get_rate_limit_per_sec"), &CommandBus::get_rate_limit_per_sec);
	ClassDB::bind_method(D_METHOD("clear_rate_limit_state"), &CommandBus::clear_rate_limit_state);
	ClassDB::bind_method(D_METHOD("get_audit_log"), &CommandBus::get_audit_log);
	ClassDB::bind_method(D_METHOD("clear_audit_log"), &CommandBus::clear_audit_log);

	BIND_ENUM_CONSTANT(PERMISSION_GUEST);
	BIND_ENUM_CONSTANT(PERMISSION_USER);
	BIND_ENUM_CONSTANT(PERMISSION_ADMIN);
	BIND_ENUM_CONSTANT(PERMISSION_DEV);
}

CommandBus::CommandBus() {
	singleton = this;
	register_builtin_commands();
}

CommandBus::~CommandBus() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
