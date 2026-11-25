/**************************************************************************/
/*  terminal_parser.h                                                     */
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

#include "core/object/ref_counted.h"

#include "modules/sandbox_core/command_bus.h"

class TerminalParser : public RefCounted {
	GDCLASS(TerminalParser, RefCounted);

	static constexpr int HISTORY_CAPACITY = 64;
	String history[HISTORY_CAPACITY];
	int history_end = 0;
	int history_size = 0;
	// Zero selects the unsubmitted draft, one selects the most recent command.
	int history_offset = 0;
	String draft;

	String _get_history_entry() const;

protected:
	static void _bind_methods();

public:
	Dictionary execute(const String &p_line, CommandBus::PermissionLevel p_tier, const StringName &p_caller, const TypedArray<StringName> &p_allowed_namespaces);
	PackedStringArray complete(const String &p_prefix, CommandBus::PermissionLevel p_tier, const TypedArray<StringName> &p_allowed_namespaces) const;
	String history_previous(const String &p_draft);
	String history_next();
};
