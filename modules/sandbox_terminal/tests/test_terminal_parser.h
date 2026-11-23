/**************************************************************************/
/*  test_terminal_parser.h                                                */
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

#include "../terminal_parser.h"

#include "core/object/callable_mp.h"
#include "tests/test_macros.h"

namespace TestTerminalParser {

static Dictionary capture_argument(const Variant &p_argument) {
	Dictionary result;
	result["ok"] = true;
	result["error_code"] = OK;
	result["output"] = "";
	result["value"] = p_argument;
	return result;
}

TEST_CASE("[SandboxTerminal] CLI quoting and scalar literals preserve declared argument types") {
	CommandBus *bus = CommandBus::get_singleton();
	REQUIRE(bus);
	Ref<TerminalParser> parser;
	parser.instantiate();
	const TypedArray<StringName> namespaces;
	const StringName caller("parser_literals");

	bus->register_command_native(SNAME("parser.value"), CommandBus::PERMISSION_USER, { Variant::NIL }, callable_mp_static(&capture_argument));
	bus->register_command_native(SNAME("parser.text"), CommandBus::PERMISSION_USER, { Variant::STRING }, callable_mp_static(&capture_argument));
	auto execute = [&](const String &p_line) {
		return parser->execute(p_line, CommandBus::PERMISSION_USER, caller, namespaces);
	};

	CHECK(execute("parser.value -12").get("value", Variant()).get_type() == Variant::INT);
	CHECK(int(execute("parser.value -12").get("value", 0)) == -12);
	CHECK(execute("parser.value 2.5e1").get("value", Variant()).get_type() == Variant::FLOAT);
	CHECK(double(execute("parser.value 2.5e1").get("value", 0)) == 25.0);
	CHECK(execute("parser.value true").get("value", Variant()).get_type() == Variant::BOOL);
	CHECK(execute("parser.value null").get("value", "missing").get_type() == Variant::NIL);
	CHECK(execute("parser.value 'true'").get("value", Variant()).get_type() == Variant::STRING);
	CHECK(String(execute("parser.text 001").get("value", "")) == "001");
	CHECK(String(execute("parser.value ''").get("value", "missing")).is_empty());
	CHECK(String(execute("parser.value \"a \\\"quoted\\\" word\"").get("value", "")) == "a \"quoted\" word");
	CHECK(int(execute("parser.value 'unfinished")["error_code"]) == ERR_PARSE_ERROR);
	CHECK(int(execute("parser.text a b")["error_code"]) == ERR_INVALID_PARAMETER);
	bus->unregister_command(SNAME("parser.value"));
	bus->unregister_command(SNAME("parser.text"));
}

TEST_CASE("[SandboxTerminal] Execution, completion and help share terminal permissions and namespaces") {
	Ref<TerminalParser> parser;
	parser.instantiate();
	TypedArray<StringName> state_only;
	state_only.push_back(SNAME("state"));
	const StringName caller("parser_visibility");

	PackedStringArray names = parser->complete("", CommandBus::PERMISSION_USER, state_only);
	CHECK(names.has("help"));
	CHECK(names.has("state.get"));
	CHECK_FALSE(names.has("state.set"));
	CHECK_FALSE(names.has("sim.speed"));
	CHECK(parser->complete("state.s", CommandBus::PERMISSION_ADMIN, state_only).has("state.set"));

	String help = parser->execute("help", CommandBus::PERMISSION_USER, caller, state_only)["output"];
	CHECK(help.contains("state.get"));
	CHECK_FALSE(help.contains("state.set"));
	help = parser->execute("help sim", CommandBus::PERMISSION_ADMIN, caller, state_only)["output"];
	CHECK(help.is_empty());
	CHECK(int(parser->execute("state.set x y", CommandBus::PERMISSION_USER, caller, state_only)["error_code"]) == ERR_UNAUTHORIZED);
	CHECK(int(parser->execute("sim.speed 1", CommandBus::PERMISSION_ADMIN, caller, state_only)["error_code"]) == ERR_UNAUTHORIZED);
}

TEST_CASE("[SandboxTerminal] Raw line byte cap and per-caller command rate apply to terminal submissions") {
	CommandBus *bus = CommandBus::get_singleton();
	REQUIRE(bus);
	Ref<TerminalParser> parser;
	parser.instantiate();
	const TypedArray<StringName> namespaces;
	const StringName caller("parser_limit");

	String line = "help " + String::chr(0xe9).repeat(253) + "x";
	CHECK(line.utf8().length() == CommandBus::MAX_COMMAND_BUFFER_BYTES);
	CHECK(bool(parser->execute(line, CommandBus::PERMISSION_GUEST, caller, namespaces)["ok"]));
	CHECK(int(parser->execute(line + "x", CommandBus::PERMISSION_GUEST, caller, namespaces)["error_code"]) == ERR_INVALID_DATA);
	CHECK(parser->history_previous("") == line);

	const int previous_limit = bus->get_rate_limit_per_sec();
	bus->set_rate_limit_per_sec(1);
	bus->clear_rate_limit_state();
	CHECK(bool(parser->execute("help", CommandBus::PERMISSION_GUEST, caller, namespaces)["ok"]));
	CHECK(int(bus->execute("help", CommandBus::PERMISSION_GUEST, caller)["error_code"]) == ERR_BUSY);
	CHECK(bool(parser->execute("help", CommandBus::PERMISSION_GUEST, SNAME("parser_other"), namespaces)["ok"]));
	bus->set_rate_limit_per_sec(previous_limit);
	bus->clear_rate_limit_state();
}

TEST_CASE("[SandboxTerminal] History retains the newest 64 submissions and restores the draft") {
	Ref<TerminalParser> parser;
	parser.instantiate();
	CHECK(parser->history_previous("draft") == "draft");
	const TypedArray<StringName> namespaces;
	for (int i = 0; i < 66; i++) {
		parser->execute("help " + itos(i), CommandBus::PERMISSION_GUEST, StringName("parser_history_" + itos(i / 24)), namespaces);
	}

	CHECK(parser->history_previous("draft") == "help 65");
	CHECK(parser->history_next() == "draft");
	for (int i = 65; i >= 2; i--) {
		CHECK(parser->history_previous("new draft") == "help " + itos(i));
	}
	CHECK(parser->history_previous("") == "help 2");
	for (int i = 3; i <= 65; i++) {
		CHECK(parser->history_next() == "help " + itos(i));
	}
	CHECK(parser->history_next() == "new draft");
}

} // namespace TestTerminalParser
