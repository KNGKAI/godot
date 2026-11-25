/**************************************************************************/
/*  terminal_display_3d.cpp                                               */
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

#include "terminal_display_3d.h"

#include "core/config/engine.h"
#include "core/input/input.h"
#include "core/input/input_event.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/physics/area_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/gui/color_rect.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/rich_text_label.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/3d/world_3d.h"
#include "scene/resources/font.h"
#include "scene/resources/material.h"
#include "scene/resources/shader.h"
#include "scene/theme/theme_db.h"
#include "servers/physics_3d/direct_states/physics_direct_space_state_3d.h"

#include "modules/sandbox_narrative/sandbox_actor.h"

Ref<Font> TerminalDisplay3D::shared_font;
Ref<Shader> TerminalDisplay3D::shared_shader;

TerminalDisplay3D::TerminalDisplay3D() {
	parser.instantiate();
}

void TerminalDisplay3D::release_shared_resources() {
	shared_font.unref();
	shared_shader.unref();
}

void TerminalDisplay3D::_build_display() {
	terminal_viewport = memnew(SubViewport);
	terminal_viewport->set_name("TerminalViewport");
	terminal_viewport->set_disable_3d(true);
	terminal_viewport->set_use_occlusion_culling(false);
	terminal_viewport->set_use_hdr_2d(false);
	terminal_viewport->set_scaling_3d_scale(1);
	terminal_viewport->set_handle_input_locally(true);
	terminal_viewport->set_size(resolution);
	terminal_viewport->set_oversampling_override(oversampling);
	add_child(terminal_viewport, false, INTERNAL_MODE_BACK);

	if (shared_font.is_null()) {
		Ref<FontFile> source = ThemeDB::get_singleton()->get_fallback_font();
		if (source.is_valid() && !source->get_data().is_empty()) {
			// The theme font is shared by all controls. Only our private copy uses MSDF.
			Ref<FontFile> font;
			font.instantiate();
			font->set_data(source->get_data());
			font->set_multichannel_signed_distance_field(true);
			shared_font = font;
		} else {
			Ref<SystemFont> font;
			font.instantiate();
			font->set_font_names(PackedStringArray{ "monospace" });
			font->set_multichannel_signed_distance_field(true);
			shared_font = font;
		}
		// Build the shared MSDF atlas once, before typing can synchronously rasterize a new command glyph.
		const TypedArray<RID> font_rids = shared_font->get_rids();
		if (!font_rids.is_empty()) {
			TS->font_render_range(font_rids[0], Vector2i(font_size, 0), 32, 126);
		}
	}
	ColorRect *panel = memnew(ColorRect);
	panel->set_name("Console");
	panel->set_color(Color(0.025, 0.045, 0.06));
	terminal_viewport->add_child(panel);
	panel->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	heading = memnew(Label);
	heading->set_name("Title");
	heading->set_text(title);
	heading->set_clip_text(true);
	heading->add_theme_font_override(SNAME("font"), shared_font);
	panel->add_child(heading);
	output = memnew(RichTextLabel);
	output->set_name("Output");
	output->set_use_bbcode(false);
	output->set_scroll_follow(true);
	output->add_theme_font_override(SNAME("normal_font"), shared_font);
	panel->add_child(output);
	command_line = memnew(LineEdit);
	command_line->set_name("Command");
	command_line->set_placeholder("Command");
	command_line->set_max_length(CommandBus::MAX_COMMAND_BUFFER_BYTES);
	command_line->add_theme_font_override(SNAME("font"), shared_font);
	panel->add_child(command_line);
	command_line->connect(SNAME("gui_input"), callable_mp(this, &TerminalDisplay3D::_line_gui_input));
	command_line->connect(SNAME("text_submitted"), callable_mp(this, &TerminalDisplay3D::_submitted));
	_layout();

	if (shared_shader.is_null()) {
		shared_shader.instantiate();
		shared_shader->set_code(R"(
shader_type spatial;
render_mode unshaded;
uniform sampler2D console_texture : source_color, filter_linear, repeat_disable;
void fragment() {
	vec3 color = texture(console_texture, UV).rgb;
	float row = UV.y * float(textureSize(console_texture, 0).y);
	float scan = 1.0 - 0.025 * cos(row * 3.14159) / max(1.0, fwidth(row));
	ALBEDO = color * scan;
}
)");
	}
	Ref<ShaderMaterial> surface;
	surface.instantiate();
	surface->set_shader(shared_shader);
	surface->set_shader_parameter(SNAME("console_texture"), terminal_viewport->get_texture());
	quad.instantiate();
	quad->set_size(screen_size);
	quad->set_material(surface);
	screen = memnew(MeshInstance3D);
	screen->set_name("Screen");
	screen->set_mesh(quad);
	screen->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	add_child(screen, false, INTERNAL_MODE_BACK);

	area = memnew(Area3D);
	area->set_name("ScreenArea");
	area->set_monitoring(false);
	area->set_monitorable(false);
	area->set_collision_mask(0);
	// Pointer queries below handle both cursor modes. Avoid a second pick from the host viewport.
	area->set_ray_pickable(false);
	screen_shape.instantiate();
	screen_shape->set_size(Vector3(screen_size.x, screen_size.y, 0.01));
	CollisionShape3D *shape_node = memnew(CollisionShape3D);
	shape_node->set_shape(screen_shape);
	area->add_child(shape_node);
	add_child(area, false, INTERNAL_MODE_BACK);
}

void TerminalDisplay3D::_layout() {
	if (!command_line) {
		return;
	}
	const int margin = MAX(4, font_size / 3);
	const int line_height = font_size * 2;
	heading->set_position(Vector2(margin, 0));
	heading->set_size(Vector2(resolution.x - 2 * margin, line_height));
	heading->add_theme_font_size_override(SNAME("font_size"), font_size);
	output->set_position(Vector2(margin, line_height));
	output->set_size(Vector2(resolution.x - 2 * margin, MAX(0, resolution.y - 2 * line_height - margin)));
	output->add_theme_font_size_override(SNAME("normal_font_size"), font_size);
	command_line->set_position(Vector2(margin, resolution.y - line_height - margin));
	command_line->set_size(Vector2(resolution.x - 2 * margin, line_height));
	command_line->add_theme_font_size_override(SNAME("font_size"), font_size);
}

void TerminalDisplay3D::set_screen_size(const Vector2 &p_size) {
	ERR_FAIL_COND(!p_size.is_finite() || p_size.x <= 0 || p_size.y <= 0);
	screen_size = p_size;
	if (quad.is_valid()) {
		quad->set_size(screen_size);
		screen_shape->set_size(Vector3(screen_size.x, screen_size.y, 0.01));
	}
}

void TerminalDisplay3D::set_resolution(const Vector2i &p_size) {
	resolution = p_size.max(Vector2i(256, 256));
	if (terminal_viewport) {
		terminal_viewport->set_size(resolution);
		_layout();
	}
}

void TerminalDisplay3D::set_font_size(int p_size) {
	ERR_FAIL_COND(p_size < 1 || p_size > 128);
	font_size = p_size;
	_layout();
}

void TerminalDisplay3D::set_oversampling(float p_scale) {
	ERR_FAIL_COND(!Math::is_finite(p_scale) || p_scale < 0 || p_scale > 16);
	oversampling = p_scale;
	if (terminal_viewport) {
		terminal_viewport->set_oversampling_override(p_scale);
	}
}

void TerminalDisplay3D::set_interaction_distance(real_t p_distance) {
	ERR_FAIL_COND(!Math::is_finite(p_distance) || p_distance <= 0);
	interaction_distance = p_distance;
}

void TerminalDisplay3D::set_throttle_updates(bool p_enabled) {
	throttle_updates = p_enabled;
	set_process_internal(throttle_updates);
	if (terminal_viewport && is_inside_tree()) {
		_update_visibility();
	}
}

void TerminalDisplay3D::set_permission_tier(CommandBus::PermissionLevel p_tier) {
#ifdef DEBUG_ENABLED
	ERR_FAIL_COND(p_tier < CommandBus::PERMISSION_GUEST || p_tier > CommandBus::PERMISSION_DEV);
#else
	ERR_FAIL_COND(p_tier < CommandBus::PERMISSION_GUEST || p_tier > CommandBus::PERMISSION_ADMIN);
#endif
	permission_tier = p_tier;
}

void TerminalDisplay3D::set_title(const String &p_title) {
	title = p_title;
	if (heading) {
		heading->set_text(title);
	}
}

Dictionary TerminalDisplay3D::execute_command(const String &p_line) {
	Dictionary result = parser->execute(p_line, permission_tier, StringName(vformat("terminal_%d", get_instance_id())), allowed_namespaces);
	_append_output(result.get("output", ""));
	return result;
}

void TerminalDisplay3D::_append_output(const String &p_text) {
	if (!output || p_text.is_empty()) {
		return;
	}
	output->add_text(p_text + "\n");
	while (output->get_paragraph_count() > 128) {
		output->remove_paragraph(0);
	}
}

void TerminalDisplay3D::_submitted(const String &p_line) {
	if (engaged && !p_line.strip_edges().is_empty()) {
		command_line->clear();
		execute_command(p_line);
	}
}

void TerminalDisplay3D::_line_gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventKey> key = p_event;
	if (!engaged || key.is_null() || !key->is_pressed() || key->is_command_or_control_pressed() || key->is_alt_pressed()) {
		return;
	}
	String line = command_line->get_text();
	switch (key->get_keycode()) {
		case Key::UP: {
			line = parser->history_previous(line);
		} break;
		case Key::DOWN: {
			line = parser->history_next();
		} break;
		case Key::TAB: {
			PackedStringArray choices = parser->complete(line, permission_tier, allowed_namespaces);
			if (choices.size() == 1) {
				line = choices[0] + " ";
			} else if (!choices.is_empty()) {
				_append_output(String("  ").join(choices));
			}
		} break;
		default:
			return;
	}
	command_line->set_text(line);
	command_line->set_caret_column(line.length());
	command_line->accept_event();
}

Error TerminalDisplay3D::engage(SandboxActor *p_actor) {
	ERR_FAIL_COND_V(!terminal_viewport || !is_inside_tree() || !terminal_viewport->is_inside_tree(), ERR_UNCONFIGURED);
	if (engaged) {
		return !p_actor || p_actor->get_instance_id() == locked_actor ? OK : ERR_BUSY;
	}
	SandboxActor *actor = p_actor;
	if (!actor && !actor_path.is_empty()) {
		actor = Object::cast_to<SandboxActor>(get_node_or_null(actor_path));
		ERR_FAIL_NULL_V(actor, ERR_DOES_NOT_EXIST);
	}
	if (actor && actor->is_control_locked()) {
		SubViewport *previous = get_viewport()->get_terminal_focus_viewport();
		TerminalDisplay3D *previous_terminal = previous ? Object::cast_to<TerminalDisplay3D>(previous->get_parent()) : nullptr;
		if (actor->get_control_lock_reason() != "terminal" || !previous_terminal || previous_terminal->locked_actor != actor->get_instance_id()) {
			return ERR_BUSY;
		}
	}
	const ObjectID actor_id = actor ? actor->get_instance_id() : ObjectID();
	Error err = get_viewport()->set_terminal_focus_viewport(terminal_viewport, callable_mp(this, &TerminalDisplay3D::_released));
	if (err != OK) {
		return err;
	}
	engaged = true;
	if (actor_id.is_valid()) {
		actor = ObjectDB::get_instance<SandboxActor>(actor_id);
		if (!actor || actor->is_control_locked()) {
			disengage();
			return ERR_BUSY;
		}
		locked_actor = actor_id;
		const ObjectID terminal_id = get_instance_id();
		actor->request_control_lock(true, "terminal");
		if (!ObjectDB::get_instance(terminal_id) || !engaged) {
			return ERR_BUSY;
		}
	}
	command_line->grab_focus();
	return OK;
}

void TerminalDisplay3D::_released() {
	engaged = false;
	if (command_line && command_line->is_inside_tree()) {
		command_line->release_focus();
	}
	SandboxActor *actor = ObjectDB::get_instance<SandboxActor>(locked_actor);
	locked_actor = ObjectID();
	if (actor && actor->get_control_lock_reason() == "terminal") {
		actor->request_control_lock(false, "terminal");
	}
}

void TerminalDisplay3D::disengage() {
	if (engaged && is_inside_tree() && get_viewport()->get_terminal_focus_viewport() == terminal_viewport) {
		get_viewport()->clear_terminal_focus_viewport();
	}
}
bool TerminalDisplay3D::_project_pointer(Camera3D *p_camera, const Vector2 &p_point, Vector2 &r_pixel) const {
	const Transform3D inverse = screen->get_global_transform().affine_inverse();
	const Vector3 start = inverse.xform(p_camera->project_ray_origin(p_point));
	const Vector3 dir = inverse.basis.xform(p_camera->project_ray_normal(p_point));
	if (dir.z >= -CMP_EPSILON || start.z <= 0) {
		return false;
	}
	const real_t distance = -start.z / dir.z;
	if (distance > interaction_distance) {
		return false;
	}
	const Vector3 point = start + dir * distance;
	const Vector2 uv(0.5 + point.x / screen_size.x, 0.5 - point.y / screen_size.y);
	r_pixel = uv * Vector2(resolution);
	return Rect2(Vector2(), Vector2(resolution)).has_point(r_pixel);
}

void TerminalDisplay3D::_pointer_left() {
	if (pointer_inside) {
		pointer_inside = false;
		last_motion_usec = 0;
		terminal_viewport->notify_mouse_exited();
	}
}

void TerminalDisplay3D::input(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouse> mouse = p_event;
	if (mouse.is_null() || !screen || !is_visible_in_tree()) {
		return;
	}
	Camera3D *camera = get_viewport()->get_camera_3d();
	const bool captured = Input::get_singleton()->get_mouse_mode() == Input::MouseMode::MOUSE_MODE_CAPTURED;
	const Vector2 point = captured ? get_viewport()->get_visible_rect().get_center() : mouse->get_position();
	Vector2 pixel;
	if (!camera || !_project_pointer(camera, point, pixel)) {
		_pointer_left();
		return;
	}
	// Access direct physics space only at the next physics tick. Collapse consecutive
	// motion samples but retain the order of presses and releases.
	Ref<InputEventMouseMotion> motion = mouse;
	if (motion.is_valid() && !pending_pointer_events.is_empty() && Object::cast_to<InputEventMouseMotion>(pending_pointer_events[pending_pointer_events.size() - 1].ptr())) {
		pending_pointer_events[pending_pointer_events.size() - 1] = p_event;
	} else if (pending_pointer_events.size() < 64) {
		pending_pointer_events.push_back(p_event);
	}
	set_physics_process_internal(true);
}

void TerminalDisplay3D::_pick(const Ref<InputEvent> &p_event) {
	Camera3D *camera = get_viewport()->get_camera_3d();
	Ref<InputEventMouse> mouse = p_event;
	if (!camera || !is_visible_in_tree() || mouse.is_null()) {
		return;
	}
	const bool captured = Input::get_singleton()->get_mouse_mode() == Input::MouseMode::MOUSE_MODE_CAPTURED;
	const Vector2 point = captured ? get_viewport()->get_visible_rect().get_center() : mouse->get_position();
	Vector2 pixel;
	if (!_project_pointer(camera, point, pixel)) {
		_pointer_left();
		return;
	}
	PS3DT::RayParameters ray;
	ray.from = camera->project_ray_origin(point);
	ray.to = ray.from + camera->project_ray_normal(point) * interaction_distance;
	ray.collide_with_areas = true;
	PS3DT::RayResult hit;
	PhysicsDirectSpaceState3D *space = get_world_3d()->get_direct_space_state();
	if (!space || !space->intersect_ray(ray, hit) || hit.collider_id != area->get_instance_id()) {
		_pointer_left();
		return;
	}
	const Vector3 local = screen->get_global_transform().affine_inverse().xform(hit.position);
	pixel = Vector2(0.5 + local.x / screen_size.x, 0.5 - local.y / screen_size.y) * Vector2(resolution);
	Ref<InputEventMouse> forwarded = mouse->duplicate();
	forwarded->set_position(pixel);
	forwarded->set_global_position(pixel);
	Ref<InputEventMouseMotion> motion = forwarded;
	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	if (motion.is_valid()) {
		const Vector2 relative = pointer_inside ? pixel - last_pointer : Vector2();
		const Vector2 velocity = last_motion_usec && now > last_motion_usec ? relative * (1000000.0 / double(now - last_motion_usec)) : Vector2();
		motion->set_relative(relative);
		motion->set_relative_screen_position(relative);
		motion->set_velocity(velocity);
		motion->set_screen_velocity(velocity);
	}
	last_motion_usec = now;
	last_pointer = pixel;
	if (!pointer_inside) {
		terminal_viewport->notify_mouse_entered();
		pointer_inside = true;
	}
	Ref<InputEventMouseButton> button = forwarded;
	const bool select = button.is_valid() && button->get_button_index() == MouseButton::LEFT && button->is_pressed();
	if (select && engage() != OK) {
		return;
	}
	const ObjectID terminal_id = get_instance_id();
	terminal_viewport->push_input(forwarded, true);
	if (select && ObjectDB::get_instance(terminal_id) && engaged) {
		command_line->grab_focus();
	}
}

void TerminalDisplay3D::_update_visibility() {
	if (!throttle_updates) {
		terminal_viewport->set_update_mode(SubViewport::UPDATE_WHEN_VISIBLE);
		return;
	}
	Camera3D *camera = get_viewport()->get_camera_3d();
	bool visible = is_visible_in_tree() && camera;
	if (visible) {
		const Transform3D transform = screen->get_global_transform();
		Vector3 closest = transform.affine_inverse().xform(camera->get_global_position());
		closest.x = CLAMP(closest.x, -screen_size.x * 0.5, screen_size.x * 0.5);
		closest.y = CLAMP(closest.y, -screen_size.y * 0.5, screen_size.y * 0.5);
		closest.z = 0;
		visible = camera->get_global_position().distance_squared_to(transform.xform(closest)) <= interaction_distance * interaction_distance;
		if (visible) {
			const Vector3 half_x = transform.basis.get_column(0) * (screen_size.x * 0.5);
			const Vector3 half_y = transform.basis.get_column(1) * (screen_size.y * 0.5);
			for (const Plane &plane : camera->get_frustum()) {
				if (plane.distance_to(transform.origin) > Math::abs(plane.normal.dot(half_x)) + Math::abs(plane.normal.dot(half_y))) {
					visible = false;
					break;
				}
			}
		}
	}
	const SubViewport::UpdateMode mode = visible ? SubViewport::UPDATE_WHEN_VISIBLE : SubViewport::UPDATE_DISABLED;
	if (terminal_viewport->get_update_mode() != mode) {
		terminal_viewport->set_update_mode(mode);
	}
}

void TerminalDisplay3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (!terminal_viewport) {
				_build_display();
			}
			set_process_input(!Engine::get_singleton()->is_editor_hint());
			set_process_internal(throttle_updates);
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			_update_visibility();
		} break;
		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
			LocalVector<Ref<InputEvent>> events;
			SWAP(events, pending_pointer_events);
			set_physics_process_internal(false);
			for (const Ref<InputEvent> &event : events) {
				if (is_inside_tree()) {
					_pick(event);
				}
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			disengage();
			pending_pointer_events.clear();
			pointer_inside = false;
			last_motion_usec = 0;
			set_physics_process_internal(false);
		} break;
	}
}

void TerminalDisplay3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_screen_size", "size"), &TerminalDisplay3D::set_screen_size);
	ClassDB::bind_method(D_METHOD("get_screen_size"), &TerminalDisplay3D::get_screen_size);
	ClassDB::bind_method(D_METHOD("set_resolution", "size"), &TerminalDisplay3D::set_resolution);
	ClassDB::bind_method(D_METHOD("get_resolution"), &TerminalDisplay3D::get_resolution);
	ClassDB::bind_method(D_METHOD("set_font_size", "size"), &TerminalDisplay3D::set_font_size);
	ClassDB::bind_method(D_METHOD("get_font_size"), &TerminalDisplay3D::get_font_size);
	ClassDB::bind_method(D_METHOD("set_oversampling", "scale"), &TerminalDisplay3D::set_oversampling);
	ClassDB::bind_method(D_METHOD("get_oversampling"), &TerminalDisplay3D::get_oversampling);
	ClassDB::bind_method(D_METHOD("set_title", "title"), &TerminalDisplay3D::set_title);
	ClassDB::bind_method(D_METHOD("get_title"), &TerminalDisplay3D::get_title);
	ClassDB::bind_method(D_METHOD("set_interaction_distance", "distance"), &TerminalDisplay3D::set_interaction_distance);
	ClassDB::bind_method(D_METHOD("get_interaction_distance"), &TerminalDisplay3D::get_interaction_distance);
	ClassDB::bind_method(D_METHOD("set_throttle_updates", "enabled"), &TerminalDisplay3D::set_throttle_updates);
	ClassDB::bind_method(D_METHOD("is_throttling_updates"), &TerminalDisplay3D::is_throttling_updates);
	ClassDB::bind_method(D_METHOD("set_actor_path", "path"), &TerminalDisplay3D::set_actor_path);
	ClassDB::bind_method(D_METHOD("get_actor_path"), &TerminalDisplay3D::get_actor_path);
	ClassDB::bind_method(D_METHOD("set_permission_tier", "tier"), &TerminalDisplay3D::set_permission_tier);
	ClassDB::bind_method(D_METHOD("get_permission_tier"), &TerminalDisplay3D::get_permission_tier);
	ClassDB::bind_method(D_METHOD("set_allowed_namespaces", "namespaces"), &TerminalDisplay3D::set_allowed_namespaces);
	ClassDB::bind_method(D_METHOD("get_allowed_namespaces"), &TerminalDisplay3D::get_allowed_namespaces);
	ClassDB::bind_method(D_METHOD("get_terminal_viewport"), &TerminalDisplay3D::get_terminal_viewport);
	ClassDB::bind_method(D_METHOD("get_command_line"), &TerminalDisplay3D::get_command_line);
	ClassDB::bind_method(D_METHOD("get_screen"), &TerminalDisplay3D::get_screen);
	ClassDB::bind_method(D_METHOD("engage", "actor"), &TerminalDisplay3D::engage, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("disengage"), &TerminalDisplay3D::disengage);
	ClassDB::bind_method(D_METHOD("is_engaged"), &TerminalDisplay3D::is_engaged);
	ClassDB::bind_method(D_METHOD("execute_command", "line"), &TerminalDisplay3D::execute_command);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "title"), "set_title", "get_title");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "screen_size", PROPERTY_HINT_NONE, "suffix:m"), "set_screen_size", "get_screen_size");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2I, "resolution"), "set_resolution", "get_resolution");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "font_size", PROPERTY_HINT_RANGE, "1,128,1"), "set_font_size", "get_font_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "oversampling", PROPERTY_HINT_RANGE, "0,16,0.1"), "set_oversampling", "get_oversampling");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "interaction_distance", PROPERTY_HINT_RANGE, "0.1,100,0.1,or_greater,suffix:m"), "set_interaction_distance", "get_interaction_distance");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "throttle_updates"), "set_throttle_updates", "is_throttling_updates");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "actor_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "SandboxActor"), "set_actor_path", "get_actor_path");
#ifdef DEBUG_ENABLED
	ADD_PROPERTY(PropertyInfo(Variant::INT, "permission_tier", PROPERTY_HINT_ENUM, "Guest,User,Admin,Dev"), "set_permission_tier", "get_permission_tier");
#else
	ADD_PROPERTY(PropertyInfo(Variant::INT, "permission_tier", PROPERTY_HINT_ENUM, "Guest,User,Admin"), "set_permission_tier", "get_permission_tier");
#endif
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "allowed_namespaces", PROPERTY_HINT_ARRAY_TYPE, "StringName"), "set_allowed_namespaces", "get_allowed_namespaces");
}
