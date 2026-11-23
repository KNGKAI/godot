/**************************************************************************/
/*  test_terminal_display_3d.h                                            */
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

#include "../terminal_display_3d.h"

#include "core/input/input_event.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/gui/line_edit.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/resources/font.h"
#include "scene/resources/material.h"
#include "scene/resources/shader.h"
#include "tests/test_macros.h"

#include "modules/sandbox_narrative/sandbox_actor.h"

namespace TestTerminalDisplay3D {

TEST_CASE("[SceneTree][SandboxTerminal][Gate4] Four monitors have independent 2D render targets and CLI state") {
	Node3D *room = memnew(Node3D);
	SceneTree::get_singleton()->get_root()->add_child(room);
	HashSet<RID> textures;
	TerminalDisplay3D *monitors[4] = {};
	for (int i = 0; i < 4; i++) {
		TerminalDisplay3D *monitor = memnew(TerminalDisplay3D);
		monitor->set_resolution(Vector2i(192 + i, 300 + i));
		room->add_child(monitor);
		monitors[i] = monitor;
		SubViewport *vp = monitor->get_terminal_viewport();
		REQUIRE(vp != nullptr);
		CHECK(vp->get_parent() == monitor);
		CHECK(vp->is_3d_disabled());
		CHECK_FALSE(vp->is_using_occlusion_culling());
		CHECK(vp->get_size() == Vector2i(256, 300 + i));
		CHECK(vp->get_scaling_3d_scale() == doctest::Approx(1.0));
		Ref<FontFile> font = monitor->get_command_line()->get_theme_font(SNAME("font"));
		REQUIRE(font.is_valid());
		CHECK(font->is_multichannel_signed_distance_field());
		Ref<ShaderMaterial> material = monitor->get_screen()->get_active_material(0);
		REQUIRE(material.is_valid());
		CHECK(material->get_shader()->get_code().contains("source_color"));
		CHECK(Ref<Texture2D>(material->get_shader_parameter(SNAME("console_texture"))) == vp->get_texture());
		textures.insert(vp->get_texture()->get_rid());
	}
	CHECK(textures.size() == 4);
	monitors[0]->get_command_line()->set_text("help");
	CHECK(monitors[1]->get_command_line()->get_text().is_empty());
	monitors[3]->set_resolution(Vector2i(100, 100));
	CHECK(monitors[3]->get_terminal_viewport()->get_size() == Vector2i(256, 256));
	monitors[3]->set_throttle_updates(true);
	CHECK(monitors[3]->get_terminal_viewport()->get_update_mode() == SubViewport::UPDATE_DISABLED);
	monitors[3]->set_throttle_updates(false);
	CHECK(monitors[3]->get_terminal_viewport()->get_update_mode() == SubViewport::UPDATE_WHEN_VISIBLE);
	memdelete(room);
}

TEST_CASE("[SceneTree][SandboxTerminal][Gate4] The active monitor owns the actor lock and command permissions") {
	Node3D *room = memnew(Node3D);
	SceneTree::get_singleton()->get_root()->add_child(room);
	SandboxActor *actor = memnew(SandboxActor);
	actor->set_name("Player");
	room->add_child(actor);
	TerminalDisplay3D *first = memnew(TerminalDisplay3D);
	TerminalDisplay3D *second = memnew(TerminalDisplay3D);
	room->add_child(first);
	room->add_child(second);
	first->set_actor_path(NodePath("../Player"));
	REQUIRE(first->engage() == OK);
	CHECK(actor->is_control_locked());
	CHECK(actor->get_control_lock_reason() == "terminal");
	REQUIRE(second->engage(actor) == OK);
	CHECK_FALSE(first->is_engaged());
	CHECK(second->is_engaged());
	CHECK(actor->is_control_locked());
	first->disengage();
	CHECK(actor->is_control_locked());
	Ref<InputEventAction> cancel;
	cancel.instantiate();
	cancel->set_action(SNAME("ui_cancel"));
	cancel->set_pressed(true);
	room->get_viewport()->push_input(cancel, true);
	CHECK_FALSE(second->is_engaged());
	CHECK_FALSE(actor->is_control_locked());

	TypedArray<StringName> namespaces;
	namespaces.push_back(SNAME("mission"));
	first->set_allowed_namespaces(namespaces);
	first->set_permission_tier(CommandBus::PERMISSION_GUEST);
	CHECK_FALSE(bool(first->execute_command("sim.pause")["ok"]));
	CHECK(bool(second->execute_command("help")["ok"]));
	actor->request_control_lock(true, "cutscene");
	CHECK(first->engage() == ERR_BUSY);
	CHECK(actor->get_control_lock_reason() == "cutscene");
	actor->request_control_lock(false, "cutscene");
	REQUIRE(first->engage() == OK);
	memdelete(first);
	CHECK_FALSE(actor->is_control_locked());
	CHECK(room->get_viewport()->get_terminal_focus_viewport() == nullptr);
	memdelete(room);
}

} // namespace TestTerminalDisplay3D
