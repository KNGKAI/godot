/**************************************************************************/
/*  terminal_parser.cpp                                                   */
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

#include "terminal_parser.h"

Dictionary TerminalParser::execute(const String &p_line, CommandBus::PermissionLevel p_tier, const StringName &p_caller, const TypedArray<StringName> &p_allowed_namespaces) {
	CommandBus *bus = CommandBus::get_singleton();
	if (!bus) {
		Dictionary result;
		result["ok"] = false;
		result["output"] = "CommandBus unavailable.";
		result["error_code"] = ERR_UNAVAILABLE;
		return result;
	}

	Dictionary result = bus->execute(p_line, p_tier, p_caller, p_allowed_namespaces);
	history_offset = 0;
	draft = String();
	if (int(result["error_code"]) != ERR_INVALID_DATA && !p_line.strip_edges().is_empty()) {
		history[history_end] = p_line;
		history_end = (history_end + 1) % HISTORY_CAPACITY;
		history_size = MIN(history_size + 1, HISTORY_CAPACITY);
	}
	return result;
}

PackedStringArray TerminalParser::complete(const String &p_prefix, CommandBus::PermissionLevel p_tier, const TypedArray<StringName> &p_allowed_namespaces) const {
	CommandBus *bus = CommandBus::get_singleton();
	return bus ? bus->get_autocomplete_suggestions(p_prefix, p_tier, p_allowed_namespaces) : PackedStringArray();
}

String TerminalParser::_get_history_entry() const {
	return history_offset == 0 ? draft : history[(history_end + HISTORY_CAPACITY - history_offset) % HISTORY_CAPACITY];
}

String TerminalParser::history_previous(const String &p_draft) {
	if (history_offset == 0) {
		draft = p_draft;
	}
	history_offset = MIN(history_offset + 1, history_size);
	return _get_history_entry();
}

String TerminalParser::history_next() {
	history_offset = MAX(history_offset - 1, 0);
	return _get_history_entry();
}

void TerminalParser::_bind_methods() {
	ClassDB::bind_method(D_METHOD("execute", "line", "tier", "caller", "allowed_namespaces"), &TerminalParser::execute);
	ClassDB::bind_method(D_METHOD("complete", "prefix", "tier", "allowed_namespaces"), &TerminalParser::complete);
	ClassDB::bind_method(D_METHOD("history_previous", "draft"), &TerminalParser::history_previous);
	ClassDB::bind_method(D_METHOD("history_next"), &TerminalParser::history_next);
}
