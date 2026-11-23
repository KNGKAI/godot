/**************************************************************************/
/*  test_terminal_focus.h                                                 */
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

#include "core/input/input_event.h"
#include "core/object/callable_mp.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "tests/test_macros.h"

namespace TestTerminalFocus {

class InputProbe : public Node {
protected:
	void input(const Ref<InputEvent> &p_event) override {
		last_event = p_event;
		input_count++;
	}
	void shortcut_input(const Ref<InputEvent> &p_event) override {
		shortcut_count++;
	}

public:
	Ref<InputEvent> last_event;
	int input_count = 0;
	int shortcut_count = 0;
	int release_count = 0;
	SubViewport *acquire_on_release = nullptr;

	void released() {
		release_count++;
		if (acquire_on_release) {
			get_viewport()->set_terminal_focus_viewport(acquire_on_release);
		}
	}

	InputProbe() {
		set_process_input(true);
		set_process_shortcut_input(true);
	}
};

TEST_CASE("[SceneTree][SandboxTerminal][Gate4] Focus directs keys, shortcuts and scrolling only to the terminal") {
	SubViewport *host = memnew(SubViewport);
	SceneTree::get_singleton()->get_root()->add_child(host);
	SubViewport *target = memnew(SubViewport);
	host->add_child(target);
	target->set_propagate_shortcuts_to_parent(true);
	InputProbe *gameplay = memnew(InputProbe);
	host->add_child(gameplay);
	InputProbe *terminal = memnew(InputProbe);
	target->add_child(terminal);

	Ref<InputEventMouseMotion> pointer;
	pointer.instantiate();
	const Point2 hit(41, 59);
	pointer->set_position(hit);
	target->push_input(pointer, true);

	REQUIRE(host->set_terminal_focus_viewport(target, callable_mp(gameplay, &InputProbe::released)) == OK);
	Ref<InputEventKey> key;
	key.instantiate();
	key->set_keycode(Key::Z);
	key->set_pressed(true);
	host->push_input(key, true);
	CHECK(terminal->last_event == key);
	CHECK(terminal->shortcut_count == 1);
	CHECK(host->is_input_handled());

	Ref<InputEventShortcut> shortcut;
	shortcut.instantiate();
	host->push_input(shortcut, true);
	CHECK(terminal->last_event == shortcut);
	CHECK(terminal->shortcut_count == 2);
	CHECK(host->is_input_handled());

	for (MouseButton direction : { MouseButton::WHEEL_UP, MouseButton::WHEEL_DOWN, MouseButton::WHEEL_LEFT, MouseButton::WHEEL_RIGHT }) {
		Ref<InputEventMouseButton> wheel;
		wheel.instantiate();
		wheel->set_pressed(true);
		wheel->set_button_index(direction);
		wheel->set_factor(0.5);
		wheel->set_position(Point2(800, 600));
		host->push_input(wheel, true);
		Ref<InputEventMouseButton> received = terminal->last_event;
		REQUIRE(received.is_valid());
		CHECK(received->get_button_index() == direction);
		CHECK(received->get_factor() == 0.5);
		CHECK(received->get_position() == hit);
		CHECK(received->get_global_position() == hit);
		CHECK(wheel->get_position() == Point2(800, 600));
		CHECK(host->is_input_handled());
	}
	CHECK(gameplay->input_count == 0);
	CHECK(gameplay->shortcut_count == 0);

	const int focused_events = terminal->input_count;
	host->push_input(pointer, true);
	CHECK(gameplay->last_event == pointer);
	CHECK(terminal->input_count == focused_events);

	Ref<InputEventKey> escape;
	escape.instantiate();
	escape->set_keycode(Key::ESCAPE);
	escape->set_pressed(true);
	host->push_input(escape, true);
	CHECK(host->is_input_handled());
	CHECK(host->get_terminal_focus_viewport() == nullptr);
	CHECK(gameplay->release_count == 1);
	CHECK(gameplay->input_count == 1);
	CHECK(terminal->input_count == focused_events);
	host->push_input(key, true);
	CHECK(gameplay->last_event == key);
	CHECK(terminal->input_count == focused_events);

	memdelete(host);
}

TEST_CASE("[SceneTree][SandboxTerminal] Focus rejects invalid descendants and releases exactly once during replacement and removal") {
	Window *root = SceneTree::get_singleton()->get_root();
	SubViewport *host = memnew(SubViewport);
	root->add_child(host);
	InputProbe *owner = memnew(InputProbe);
	host->add_child(owner);
	SubViewport *first = memnew(SubViewport);
	owner->add_child(first);
	SubViewport *second = memnew(SubViewport);
	host->add_child(second);
	SubViewport *other = memnew(SubViewport);
	root->add_child(other);
	const Callable released = callable_mp(owner, &InputProbe::released);

	REQUIRE(host->set_terminal_focus_viewport(first, released) == OK);
	CHECK(host->set_terminal_focus_viewport(first, released) == OK);
	CHECK(host->set_terminal_focus_viewport(host) == ERR_INVALID_PARAMETER);
	CHECK(host->set_terminal_focus_viewport(other) == ERR_INVALID_PARAMETER);
	CHECK(first->set_terminal_focus_viewport(host) == ERR_INVALID_PARAMETER);
	CHECK(host->set_terminal_focus_viewport(nullptr) == ERR_INVALID_PARAMETER);
	CHECK(host->get_terminal_focus_viewport() == first);
	CHECK(owner->release_count == 0);

	owner->acquire_on_release = first;
	CHECK(host->set_terminal_focus_viewport(second) == ERR_BUSY);
	CHECK(host->get_terminal_focus_viewport() == first);
	CHECK(owner->release_count == 1);
	owner->acquire_on_release = nullptr;
	REQUIRE(host->set_terminal_focus_viewport(second, released) == OK);
	REQUIRE(host->set_terminal_focus_viewport(first, released) == OK);
	CHECK(owner->release_count == 2);
	owner->remove_child(first);
	CHECK(host->get_terminal_focus_viewport() == nullptr);
	CHECK(owner->release_count == 3);
	CHECK(host->set_terminal_focus_viewport(first) == ERR_INVALID_PARAMETER);
	host->clear_terminal_focus_viewport();
	CHECK(owner->release_count == 3);

	REQUIRE(host->set_terminal_focus_viewport(second, released) == OK);
	root->remove_child(host);
	CHECK(host->get_terminal_focus_viewport() == nullptr);
	CHECK(owner->release_count == 4);
	memdelete(host);
	memdelete(first);
	memdelete(other);
}

} // namespace TestTerminalFocus
