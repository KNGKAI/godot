/**************************************************************************/
/*  terminal_display_3d.h                                                 */
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

#include "terminal_parser.h"

#include "scene/3d/node_3d.h"

class Area3D;
class BoxShape3D;
class Camera3D;
class Control;
class Font;
class Label;
class LineEdit;
class MeshInstance3D;
class QuadMesh;
class RichTextLabel;
class SandboxActor;
class Shader;
class SubViewport;

class TerminalDisplay3D : public Node3D {
	GDCLASS(TerminalDisplay3D, Node3D);

	Ref<TerminalParser> parser;
	Ref<QuadMesh> quad;
	Ref<BoxShape3D> screen_shape;
	SubViewport *terminal_viewport = nullptr;
	MeshInstance3D *screen = nullptr;
	Area3D *area = nullptr;
	Label *heading = nullptr;
	RichTextLabel *output = nullptr;
	LineEdit *command_line = nullptr;
	String title = "Terminal";
	Vector2 screen_size = Vector2(2.4, 1.35);
	Vector2i resolution = Vector2i(768, 432);
	int font_size = 36;
	float oversampling = 1;
	real_t interaction_distance = 10;
	bool throttle_updates = true;
	NodePath actor_path;
	ObjectID locked_actor;
	bool engaged = false;
	bool pointer_inside = false;
	Vector2 last_pointer;
	uint64_t last_motion_usec = 0;
	LocalVector<Ref<InputEvent>> pending_pointer_events;
	CommandBus::PermissionLevel permission_tier = CommandBus::PERMISSION_USER;
	TypedArray<StringName> allowed_namespaces;

	static Ref<Font> shared_font;
	static Ref<Shader> shared_shader;
	void _build_display();
	void _layout();
	void _released();
	void _submitted(const String &p_line);
	void _line_gui_input(const Ref<InputEvent> &p_event);
	void _append_output(const String &p_text);
	void _update_visibility();
	void _pick(const Ref<InputEvent> &p_event);
	void _pointer_left();
	bool _project_pointer(Camera3D *p_camera, const Vector2 &p_point, Vector2 &r_pixel) const;

protected:
	static void _bind_methods();
	void _notification(int p_what);
	void input(const Ref<InputEvent> &p_event) override;

public:
	void set_screen_size(const Vector2 &p_size);
	Vector2 get_screen_size() const { return screen_size; }
	void set_resolution(const Vector2i &p_size);
	Vector2i get_resolution() const { return resolution; }
	void set_font_size(int p_size);
	int get_font_size() const { return font_size; }
	void set_oversampling(float p_scale);
	float get_oversampling() const { return oversampling; }
	void set_title(const String &p_title);
	String get_title() const { return title; }
	void set_interaction_distance(real_t p_distance);
	real_t get_interaction_distance() const { return interaction_distance; }
	void set_throttle_updates(bool p_enabled);
	bool is_throttling_updates() const { return throttle_updates; }
	void set_actor_path(const NodePath &p_path) { actor_path = p_path; }
	NodePath get_actor_path() const { return actor_path; }
	void set_permission_tier(CommandBus::PermissionLevel p_tier);
	CommandBus::PermissionLevel get_permission_tier() const { return permission_tier; }
	void set_allowed_namespaces(const TypedArray<StringName> &p_namespaces) { allowed_namespaces = p_namespaces; }
	TypedArray<StringName> get_allowed_namespaces() const { return allowed_namespaces; }

	SubViewport *get_terminal_viewport() const { return terminal_viewport; }
	LineEdit *get_command_line() const { return command_line; }
	MeshInstance3D *get_screen() const { return screen; }
	bool is_engaged() const { return engaged; }
	Error engage(SandboxActor *p_actor = nullptr);
	void disengage();
	Dictionary execute_command(const String &p_line);
	static void release_shared_resources();
	TerminalDisplay3D();
};
