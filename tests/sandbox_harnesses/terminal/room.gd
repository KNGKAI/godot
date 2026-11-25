extends Node3D

const TITLES := ["PUMP 642", "LINK 835", "CORE 713", "PORT 259"]
var output_dir := "res://runs/"

class InputObserver extends Node:
	var wheel_presses := 0
	var motion: InputEventMouseMotion

	func _input(event: InputEvent) -> void:
		if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_WHEEL_DOWN and event.pressed:
			wheel_presses += 1
		if event is InputEventMouseMotion:
			motion = event

@onready var camera: Camera3D = $Camera
@onready var actor: SandboxActor = $Actor
var terminals: Array[TerminalDisplay3D] = []
var observations: Array[Dictionary] = []
var failures: Array[String] = []
var locks: Array[bool] = []
var commands: Array[String] = []
var report := {}

func check(ok: bool, description: String) -> void:
	if not ok:
		failures.append(description)
		push_error(description)

func frames(count: int) -> void:
	for i in count:
		await RenderingServer.frame_post_draw

func physics_ticks(count: int) -> void:
	for i in count:
		await get_tree().physics_frame

func room_box(center: Vector3, size: Vector3, color: Color) -> void:
	var box := BoxMesh.new()
	box.size = size
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	box.material = material
	var instance := MeshInstance3D.new()
	instance.mesh = box
	instance.position = center
	add_child(instance)

func make_room() -> void:
	room_box(Vector3(0, 2.5, -0.25), Vector3(28, 5, 0.25), Color("#586575"))
	room_box(Vector3(0, -0.2, 4), Vector3(28, 0.4, 12), Color("#455262"))
	room_box(Vector3(0, 5.3, 4), Vector3(28, 0.3, 12), Color("#73818c"))
	for side in [-1, 1]:
		room_box(Vector3(side * 14, 2.5, 4), Vector3(0.3, 5, 12), Color("#526073"))
	var wall := OccluderInstance3D.new()
	var wall_bounds := BoxOccluder3D.new()
	wall_bounds.size = Vector3(28, 5, 0.25)
	wall.occluder = wall_bounds
	wall.position = Vector3(0, 2.5, -0.25)
	add_child(wall)
	for i in TITLES.size():
		var terminal := TerminalDisplay3D.new()
		terminal.name = "Console%d" % i
		terminal.title = TITLES[i]
		terminal.position = Vector3(-1.32 + (i % 2) * 2.64, 2.9 - (i / 2) * 1.6, 0)
		terminal.actor_path = ^"../Actor"
		add_child(terminal)
		terminals.append(terminal)
	actor.control_lock_requested.connect(func(locked: bool, reason: StringName) -> void:
		if reason == &"terminal":
			locks.append(locked)
	)

func overview() -> void:
	camera.position = Vector3(0, 2.1, 5.0)
	camera.look_at(Vector3(0, 2.1, 0))

func trace_samples(expected: int) -> Array[Dictionary]:
	# GPU timestamps arrive several frames after the scene change.
	await frames(15)
	var device := RenderingServer.get_rendering_device()
	var samples: Array[Dictionary] = []
	for i in 8:
		await frames(1)
		var names: Array[String] = []
		for index in device.get_captured_timestamps_count():
			names.append(device.get_captured_timestamp_name(index))
		var passes := names.count("Update Occlusion Buffer")
		var scenes := names.count("> Render 3D Scene")
		var viewports := 0
		for name in names:
			if name.begins_with("> Render Viewport "):
				viewports += 1
		var frame := device.get_captured_timestamps_frame()
		check(samples.is_empty() or frame > samples.back().frame, "Missing distinct GPU profile frame")
		check(viewports == 5, "Expected all four terminals and the room to render in this frame")
		check(passes == expected and scenes == expected, "Expected %d scene and occlusion passes, got %d / %d" % [expected, scenes, passes])
		samples.append({"frame": frame, "viewports": viewports, "occlusion": passes, "scenes": scenes})
	return samples

func profile_room() -> void:
	overview()
	await frames(8)
	var viewports: Array[SubViewport] = []
	for terminal in terminals:
		var viewport := terminal.get_terminal_viewport()
		check(viewport.disable_3d, "Terminal viewport must disable 3D")
		check(viewport.render_target_update_mode != SubViewport.UPDATE_DISABLED, "All four screens must be rendering")
		check(terminal.find_children("*", "WorldEnvironment", true, false).is_empty(), "Terminal has its own WorldEnvironment")
		viewports.append(viewport)
	var overview_image := get_viewport().get_texture().get_image()
	check(overview_image.save_png(output_dir + "room.png") == OK, "Could not write room capture")
	var traces := {}
	traces["room"] = await trace_samples(1)
	# The same four viewports get active cameras looking away from the terminals.
	# This makes their ability to render 3D independent of their normal camera setup.
	var cameras: Array[Camera3D] = []
	for viewport in viewports:
		var view_camera := Camera3D.new()
		view_camera.position = Vector3(0, 2, 5)
		view_camera.rotation.y = PI * 0.5
		viewport.add_child(view_camera)
		view_camera.current = true
		viewport.use_occlusion_culling = true
		viewport.disable_3d = false
		cameras.append(view_camera)
	traces["four_3d_controls"] = await trace_samples(5)
	for viewport in viewports:
		viewport.disable_3d = true
	traces["four_disabled_with_cameras"] = await trace_samples(1)
	for view_camera in cameras:
		view_camera.queue_free()
	for viewport in viewports:
		viewport.use_occlusion_culling = false
	report["renderer_trace"] = traces

func screen_point(terminal: TerminalDisplay3D, pixel: Vector2) -> Vector3:
	var uv := pixel / Vector2(terminal.get_terminal_viewport().size)
	return terminal.get_screen().to_global(Vector3((uv.x - 0.5) * terminal.screen_size.x, (0.5 - uv.y) * terminal.screen_size.y, 0))

func title_rect(terminal: TerminalDisplay3D) -> Rect2:
	for label: Label in terminal.find_children("*", "Label", true, false):
		if label.text == terminal.title:
			var first := label.get_character_bounds(0)
			var last := label.get_character_bounds(label.text.length() - 1)
			var bounds := first.merge(last).grow(3.0)
			bounds.position += label.global_position
			return bounds
	return Rect2()

func capture_title(terminal: TerminalDisplay3D, bounds: Rect2, distance: int, angle: int) -> void:
	var target := screen_point(terminal, bounds.get_center())
	var direction := Vector3.BACK.rotated(Vector3.UP, deg_to_rad(angle))
	camera.position = target + direction * distance
	camera.look_at(target)
	await frames(4)
	var screenshot := get_viewport().get_texture().get_image()
	var corners := [bounds.position, Vector2(bounds.end.x, bounds.position.y), bounds.end, Vector2(bounds.position.x, bounds.end.y)]
	var projection := []
	var crop := Rect2()
	for i in corners.size():
		var point := screen_point(terminal, corners[i])
		var pixel := camera.unproject_position(point)
		var depth := -camera.to_local(point).z
		projection.append([pixel.x, pixel.y, depth])
		crop = Rect2(pixel, Vector2.ZERO) if i == 0 else crop.expand(pixel)
	var region := Rect2i(Vector2i(crop.position.floor()) - Vector2i(2, 2), Vector2i(crop.size.ceil()) + Vector2i(5, 5))
	check(Rect2i(Vector2i.ZERO, screenshot.get_size()).encloses(region), "Title falls outside the rendered frame")
	var filename := "%s-%dm-%ddeg.png" % [terminal.name, distance, angle]
	check(screenshot.get_region(region).save_png(output_dir + filename) == OK, "Could not save " + filename)
	observations.append({
		"file": filename, "expected": terminal.title, "distance_m": distance, "angle_deg": angle,
		"origin": [region.position.x, region.position.y], "corners": projection,
		"label_size": [bounds.size.x, bounds.size.y]
	})

func click_at_offset(pressed: bool) -> void:
	var event := InputEventMouseButton.new()
	event.button_index = MOUSE_BUTTON_LEFT
	event.pressed = pressed
	event.position = Vector2(1, 1)
	get_viewport().push_input(event, true)

func press_key(code: Key, unicode: int = 0) -> void:
	for pressed in [true, false]:
		var event := InputEventKey.new()
		event.keycode = code
		event.unicode = unicode
		event.pressed = pressed
		get_viewport().push_input(event, true)

func probe_command(value: String) -> String:
	commands.append(value)
	return value

func exercise_input() -> void:
	var terminal := terminals[0]
	camera.position = terminal.global_position + Vector3.BACK * 3.0
	camera.look_at(terminal.global_position)
	await physics_ticks(3)
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	await frames(3)
	check(Input.mouse_mode == Input.MOUSE_MODE_CAPTURED, "Mouse capture unavailable")
	var blocker := StaticBody3D.new()
	var shape := CollisionShape3D.new()
	shape.shape = BoxShape3D.new()
	blocker.add_child(shape)
	blocker.position = terminal.global_position + Vector3.BACK * 1.5
	add_child(blocker)
	await physics_ticks(3)
	click_at_offset(true)
	await physics_ticks(3)
	click_at_offset(false)
	check(not terminal.is_engaged() and locks.is_empty(), "Captured click engaged through the solid blocker")
	blocker.queue_free()
	await physics_ticks(3)
	click_at_offset(true)
	await physics_ticks(3)
	click_at_offset(false)
	await physics_ticks(2)
	check(terminal.is_engaged() and locks == [true], "Unobstructed center ray failed to lock actor")
	var observer := InputObserver.new()
	terminal.get_terminal_viewport().add_child(observer)
	var size_px := Vector2(terminal.resolution)
	var expected := size_px * Vector2(0.625, 0.425)
	camera.look_at(screen_point(terminal, expected))
	var motion := InputEventMouseMotion.new()
	motion.position = Vector2(1, 1)
	motion.relative = Vector2(-64, 32)
	get_viewport().push_input(motion, true)
	await physics_ticks(3)
	var hit_px: Array[float] = []
	if observer.motion:
		var hit := observer.motion.position
		hit_px.assign([hit.x, hit.y])
		check(hit.distance_to(expected) < 2.0, "Captured motion did not reach the camera aim point")
		check(observer.motion.relative.distance_to(expected - size_px * 0.5) < 2.0, "Incorrect UV-space relative motion")
		check(observer.motion.velocity.dot(observer.motion.relative) > 0.0, "Missing UV-space motion velocity")
	else:
		check(false, "The terminal received no captured motion")
	var wheel := InputEventMouseButton.new()
	wheel.button_index = MOUSE_BUTTON_WHEEL_DOWN
	wheel.pressed = true
	get_viewport().push_input(wheel, true)
	check(observer.wheel_presses == 1, "Root wheel did not reach the focused viewport once")
	for letter in "help":
		press_key(letter.to_upper().unicode_at(0) as Key, letter.unicode_at(0))
	check(terminal.get_command_line().text == "help", "Root typing failed to enter help in the focused command line")
	var audit_before := CommandBus.get_audit_log().size()
	press_key(KEY_ENTER)
	var audit := CommandBus.get_audit_log()
	check(audit.size() == audit_before + 1 and audit.back().command_string == "help" and audit.back().ok, "Root Enter failed to execute help exactly once")
	press_key(KEY_ESCAPE)
	check(not terminal.is_engaged() and locks == [true, false], "Escape failed to unlock the actor")
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	var signature: Array[int] = [TYPE_STRING]
	CommandBus.register_command(&"probe.echo", CommandBus.PERMISSION_GUEST, signature, probe_command, "Echo a captured argument")
	var namespaces: Array[StringName] = [&"probe"]
	terminal.allowed_namespaces = namespaces
	var allowed := terminal.execute_command('probe.echo "north gate"')
	# state.get is permitted at the default USER tier. Only the namespace excludes it.
	var denied := terminal.execute_command("state.get gate4.example")
	check(allowed.ok and allowed.output == "north gate" and commands == ["north gate"], "GDScript handler or quoted argument failed")
	check(not denied.ok and denied.error_code == ERR_UNAUTHORIZED, "Terminal accepted a command outside its namespace")
	CommandBus.unregister_command(&"probe.echo")
	report["input"] = {"actor_locks": locks, "motion_pixel": hit_px, "wheel_deliveries": observer.wheel_presses, "command_arguments": commands, "namespace_denial": denied.error_code}

func throttle_sample(viewport: SubViewport) -> Dictionary:
	await frames(12)
	var device := RenderingServer.get_rendering_device()
	var marker := "vp_begin_%d" % viewport.get_viewport_rid().get_id()
	var draws := 0
	for i in 4:
		await frames(1)
		for index in device.get_captured_timestamps_count():
			if device.get_captured_timestamp_name(index) == marker:
				draws += 1
	return {"mode": viewport.render_target_update_mode, "draws_in_four_frames": draws}

func exercise_throttling() -> void:
	var terminal := terminals[0]
	var viewport := terminal.get_terminal_viewport()
	RenderingServer.viewport_set_measure_render_time(viewport.get_viewport_rid(), true)
	camera.position = terminal.position + Vector3.BACK * (terminal.interaction_distance + 5)
	camera.look_at(terminal.position)
	var distant := await throttle_sample(viewport)
	camera.position = terminal.position + Vector3.BACK * 3.0
	camera.look_at(camera.position + Vector3.BACK)
	var behind := await throttle_sample(viewport)
	camera.look_at(terminal.position)
	var nearby := await throttle_sample(viewport)
	check(distant.mode == SubViewport.UPDATE_DISABLED and behind.mode == SubViewport.UPDATE_DISABLED and nearby.mode != SubViewport.UPDATE_DISABLED, "Distance/frustum throttling or recovery failed")
	check(distant.draws_in_four_frames == 0 and behind.draws_in_four_frames == 0 and nearby.draws_in_four_frames == 4, "Renderer did not follow terminal update mode")
	RenderingServer.viewport_set_measure_render_time(viewport.get_viewport_rid(), false)
	report["throttling"] = {"distant": distant, "behind_camera": behind, "nearby": nearby}

func _ready() -> void:
	if RenderingServer.get_rendering_device() == null:
		push_error("Gate 4 needs a rendering device with GPU profiling.")
		get_tree().quit(2)
		return
	var args := OS.get_cmdline_user_args()
	if args.size() == 1:
		output_dir = args[0]
		if not output_dir.ends_with("/"):
			output_dir += "/"
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(output_dir))
	make_room()
	await profile_room()
	await exercise_input()
	await exercise_throttling()
	for terminal in terminals:
		var bounds := title_rect(terminal)
		check(bounds.has_area(), "Missing terminal title label " + terminal.title)
		if bounds.has_area():
			for distance in range(1, 9):
				for angle: int in [0, 45]:
					await capture_title(terminal, bounds, distance, angle)
	report.merge({
		"engine": Engine.get_version_info().string, "gpu": RenderingServer.get_video_adapter_name(),
		"driver": RenderingServer.get_video_adapter_api_version(), "renderer": RenderingServer.get_current_rendering_method(),
		"window": [get_viewport().size.x, get_viewport().size.y], "vertical_fov": camera.fov,
		"screen_size_m": [terminals[0].screen_size.x, terminals[0].screen_size.y],
		"terminal_pixels": [terminals[0].resolution.x, terminals[0].resolution.y], "font_size": terminals[0].font_size,
		"observations": observations, "failures": failures
	})
	var file := FileAccess.open(output_dir + "room.json", FileAccess.WRITE)
	file.store_string(JSON.stringify(report, "\t") + "\n")
	file.close()
	print("GATE4 rendered_captures=", observations.size(), " failures=", failures)
	get_tree().quit(0 if failures.is_empty() and observations.size() == 64 else 1)
