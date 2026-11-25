extends Node

const START_X := 225.0
const APPROACH_X := 1225.0
const END_X := 15775.0
const ROAD_Z := 8000.0
const SPEED := 90.0
const SETTLE_US := 4_000_000
const TIMEOUT_US := 420_000_000
const WARMUP_TURN_X := 5250.0

@onready var rig: Node3D = $Rig
@onready var camera: Camera3D = $Rig/Camera

var stream: Object
var shifter: Object
var visual := false
var report_name := "timing"
var stage := "warm"
var travel_x := APPROACH_X
var start_us: int
var stable_since_us: int
var last_frame_us: int
var travel_start_us: int
var travel_end_us: int
var frame_intervals := PackedInt64Array()
var max_activation_ms := 0.0
var activation_over_budget := 0
var max_activation_x := 0.0
var before := {}
var before_residents := 0
var initial_scenes := {}
var warm_district := false
var cold := {}
var cold_scenes := {}
var warm_peak := 0
var frame_positions := PackedFloat32Array()
var frame_activation_us := PackedInt32Array()
var peak_bytes := 0
var peak_dense_texture_bytes := 0
var peak_residents := 0
var max_draw_calls := 0
var previous_origin := PackedFloat64Array()
var shifts := PackedFloat64Array()
var visual_frames := 0
var marker_samples := 0
var bad_visual_frames := 0
var bad_shift_frames := 0
var shift_window_frames := 0
var shift_window_remaining := 0
var fading_frames := 0
var road_fails := 0
var roof_fails := 0
var terrain_fails := 0
var terrain_pixels := 0
var clear_color_detected := false
var first_bad := {}
var previous_image: Image
var run_config := {}
var settled_windows := {}
var saved_fade := false

func _ready() -> void:
	start_us = Time.get_ticks_usec()
	stable_since_us = start_us
	stream = Engine.get_singleton("StreamingServer")
	shifter = Engine.get_singleton("OriginShifter")
	if not stream or not shifter or RenderingServer.get_rendering_device() == null:
		fail("Requires the sandbox streaming module and a hardware RenderingDevice")
		return
	visual = "--visual" in OS.get_cmdline_user_args()
	if visual:
		report_name = "visual"
	elif "--memory" in OS.get_cmdline_user_args():
		report_name = "memory"
	warm_district = "--warm-district" in OS.get_cmdline_user_args()
	if visual:
		$Environment.environment.background_color = Color.MAGENTA
		camera.projection = Camera3D.PROJECTION_ORTHOGONAL
		camera.position = Vector3(0, 600, 0)
		camera.rotation_degrees = Vector3(-90, 0, 0)
		camera.size = 745.0
	if not ResourceLoader.exists("res://world/manifest.tres"):
		fail("Generate the benchmark world before running the project")
		return
	if stream.set_manifest(load("res://world/manifest.tres")) != OK:
		fail("Could not register the generated partition manifest")
		return
	shifter.set_shift_threshold(2048.0)
	shifter.register_root(rig)
	stream.set_enabled(true)
	RenderingServer.frame_post_draw.connect(observe_frame)
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path("res://results"))
	run_config = {
		"mode": report_name,
		"engine": Engine.get_version_info().string,
		"project_arguments": Array(OS.get_cmdline_user_args()),
		"executable": OS.get_executable_path().get_file(),
		"pid": OS.get_process_id(),
		"executable_sha256": FileAccess.get_sha256(OS.get_executable_path()),
		"manifest_sha256": FileAccess.get_sha256("res://world/manifest.tres"),
		"utc": Time.get_datetime_string_from_system(true),
		"gpu": RenderingServer.get_video_adapter_name(),
		"driver": RenderingServer.get_current_rendering_driver_name(),
		"audio_driver": AudioServer.get_driver_name(),
		"display_driver": DisplayServer.get_name(),
		"renderer": RenderingServer.get_current_rendering_method(),
		"resolution": str(get_viewport().get_visible_rect().size),
		"physics_ticks_per_second": Engine.physics_ticks_per_second,
		"fps_limit": Engine.max_fps,
		"vsync_mode": DisplayServer.window_get_vsync_mode(),
	}
	print("GATE6 start ", JSON.stringify(run_config))

func _physics_process(delta: float) -> void:
	if stage == "approach" or stage == "warm_return":
		travel_x = maxf(START_X, travel_x - SPEED * delta)
		if travel_x == START_X:
			stage = "prepare"
			stable_since_us = Time.get_ticks_usec()
	elif stage == "warm_out":
		travel_x = minf(WARMUP_TURN_X, travel_x + SPEED * delta)
		if travel_x == WARMUP_TURN_X:
			stage = "warm_return"
	elif stage == "travel":
		travel_x = minf(END_X, travel_x + SPEED * delta)
	else:
		return
	var origin: PackedFloat64Array = shifter.get_origin_64()
	rig.global_position = Vector3(travel_x - origin[0], -origin[1], ROAD_Z - origin[2])

func memory() -> Dictionary:
	return {
		"video_bytes": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_VIDEO_MEM_USED),
		"texture_bytes": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TEXTURE_MEM_USED),
		"buffer_bytes": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_BUFFER_MEM_USED),
	}

func observe_frame() -> void:
	if stage == "done":
		return
	var now := Time.get_ticks_usec()
	if now - start_us > TIMEOUT_US:
		fail("Timed out during " + stage)
		return
	var stats: Dictionary = stream.get_stats()
	if int(stats.failed_cells) > 0:
		fail("Resource load failed at x=%s: %s" % [travel_x, stats])
		return
	if stage == "approach":
		return
	if stage == "warm_out" or stage == "warm_return":
		warm_peak = maxi(warm_peak, int(stats.renderer_video_memory_bytes))
		return
	if stage != "travel":
		if int(stats.resident_cells) == 0 or int(stats.loading_cells) > 0 or int(stats.queue_depth) > 0:
			stable_since_us = now
		elif now - stable_since_us >= SETTLE_US:
			if stage == "warm":
				stage = "approach"
			elif stage == "prepare":
				if warm_district and cold.is_empty():
					record_settle_window("cold", now)
					cold = memory()
					cold_scenes = resident_scenes()
					stage = "warm_out"
					print("GATE6 cold rural baseline ", cold)
					return
				record_settle_window("before", now)
				before = memory()
				before_residents = stats.resident_cells
				initial_scenes = resident_scenes()
				previous_origin = shifter.get_origin_64()
				stage = "travel"
				travel_start_us = now
				last_frame_us = now
				print("GATE6 start baseline ", before, " resident_cells=", stats.resident_cells)
			else:
				record_settle_window("after", now)
				finish(stats)
		return

	frame_intervals.append(now - last_frame_us)
	last_frame_us = now
	var activation: float = stats.activation_ms
	if not visual:
		frame_positions.append(travel_x)
		frame_activation_us.append(roundi(activation * 1000.0))
	if activation > max_activation_ms:
		max_activation_ms = activation
		max_activation_x = travel_x
	activation_over_budget += int(activation > 2.0)
	peak_residents = maxi(peak_residents, int(stats.resident_cells))
	max_draw_calls = maxi(max_draw_calls, RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_DRAW_CALLS_IN_FRAME))
	if travel_x >= 4000.0 and travel_x < 11500.0:
		peak_bytes = maxi(peak_bytes, int(stats.renderer_video_memory_bytes))
		peak_dense_texture_bytes = maxi(peak_dense_texture_bytes, RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TEXTURE_MEM_USED))
	var origin: PackedFloat64Array = shifter.get_origin_64()
	if origin != previous_origin:
		shifts.append(travel_x)
		previous_origin = origin
		shift_window_remaining = 4
		print("GATE6 origin shift x=", travel_x, " origin=", origin)
	if visual:
		observe_pixels(origin)
		if stage == "done":
			return
	if travel_x >= END_X:
		travel_end_us = now
		stage = "settle"
		stable_since_us = now

func record_settle_window(label: String, now: int) -> void:
	var unix_s := Time.get_unix_time_from_system()
	settled_windows[label] = {
		"from_tick_us": stable_since_us, "observed_tick_us": now,
		"from_unix_s": unix_s - (now - stable_since_us) / 1e6,
		"observed_unix_s": unix_s,
	}

func observe_pixels(origin: PackedFloat64Array) -> void:
	var image := get_viewport().get_texture().get_image()
	if not image or image.is_empty():
		fail("GPU viewport readback failed")
		return
	visual_frames += 1
	var failures := 0
	# Native packed-array scanning checks every pixel of the expected opaque road corridor.
	# At entry the left edge of the image lies outside the 16 km world (positive control).
	if visual_frames == 1:
		clear_color_detected = has_clear_color(image.get_region(Rect2i(2, 2, 6, 6)))
	var west := maxf(3.0, travel_x - 440.0)
	var east := minf(15997.0, travel_x + 510.0)
	var corner_a := camera.unproject_position(Vector3(west - origin[0], 0, ROAD_Z - origin[2] - 110))
	var corner_b := camera.unproject_position(Vector3(east - origin[0], 0, ROAD_Z - origin[2] + 110))
	var region := Rect2i(Vector2i(corner_a.ceil()), Vector2i((corner_b - corner_a).floor()))
	if not Rect2i(Vector2i.ZERO, image.get_size()).encloses(region) or not region.has_area():
		fail("Road corridor fell outside viewport at x=%s" % travel_x)
		return
	terrain_pixels += region.get_area()
	if has_clear_color(image.get_region(region)):
		terrain_fails += 1
		failures += 1
	var low_local_x := travel_x - origin[0] - 510.0
	var high_local_x := travel_x - origin[0] + 555.0
	for mark: GeometryInstance3D in get_tree().get_nodes_in_group(&"gate6_probe"):
		if not mark.is_visible_in_tree() or mark.global_position.x < low_local_x or mark.global_position.x > high_local_x:
			continue
		var fade: Variant = mark.get_instance_shader_parameter(&"hlod_fade")
		if fade is float and fade > 0.0 and fade < 1.0:
			fading_frames += 1
			if not saved_fade and travel_x > 4600.0 and travel_x < 10800.0:
				save_capture(image, "urban-hlod")
				saved_fade = true
			break
	var first := maxi(0, ceili((low_local_x + origin[0] - 125.0) / 250.0))
	var last := mini(63, floori((high_local_x + origin[0] - 125.0) / 250.0))
	for column in range(first, last + 1):
		var x := 125.0 + 250.0 * column
		var local := Vector3(x - origin[0], 0.08 - origin[1], ROAD_Z - origin[2])
		if not check_marker(image, local, false):
			road_fails += 1
			failures += 1
		if x >= 4000.0 and x < 11500.0:
			local += Vector3(0, 17.92, 35)
			if not check_marker(image, local, true):
				roof_fails += 1
				failures += 1
	if failures > 0:
		bad_visual_frames += 1
		if first_bad.is_empty():
			first_bad = {"x": travel_x, "origin": str(origin), "frame": visual_frames}
			save_capture(image, "first-mismatch")
	if shift_window_remaining > 0:
		if shifts.size() == 3:
			if shift_window_remaining == 4:
				if previous_image != null:
					save_capture(previous_image, "before-shift")
				save_capture(image, "shift")
			elif shift_window_remaining == 3:
				save_capture(image, "after-shift")
		shift_window_frames += 1
		bad_shift_frames += int(failures > 0)
		shift_window_remaining -= 1
	previous_image = image

func has_clear_color(region: Image) -> bool:
	region.convert(Image.FORMAT_RGBA8)
	# Opaque magenta (FF 00 FF FF) as a signed little-endian 32-bit integer.
	return region.get_data().to_int32_array().has(-65281)

func resident_scenes() -> Dictionary:
	var workload := {}
	var nodes: Array[Node] = [self]
	while not nodes.is_empty():
		var node: Node = nodes.pop_back()
		nodes.append_array(node.get_children(true))
		var path := node.scene_file_path
		if path.begins_with("res://world/"):
			var key := path.trim_prefix("res://world/")
			workload[key] = int(workload.get(key, 0)) + 1
	return workload

func dense_textures_still_cached() -> int:
	var cached := 0
	for x in range(16, 46):
		for z in range(30, 34):
			for tier: String in ["high", "mid", "proxy"]:
				var path := "res://world/town_%d_%d/%s.dds" % [x, z, tier]
				cached += int(ResourceLoader.has_cached(path))
	return cached

func save_capture(image: Image, label: String) -> void:
	var path := "res://results/%s.png" % label
	if image.save_png(path) != OK:
		push_error("Cannot write " + path)

func check_marker(image: Image, center: Vector3, roof: bool) -> bool:
	# A colored footprint can disappear or jump sideways during a bad swap/rebase.
	# Probe both its interior and its exterior against its known world dimensions.
	var screen := camera.unproject_position(center)
	var pixels_per_meter := float(image.get_height()) / camera.size
	var interior := 4.0 if roof else 2.0
	var exterior := 14.0 if roof else 11.0
	var ok := true
	for offset: Vector2 in [Vector2.ZERO, Vector2(-interior, -interior), Vector2(interior, interior),
			Vector2(-interior, interior), Vector2(interior, -interior), Vector2(-exterior, 0), Vector2(exterior, 0)]:
		var point := Vector2i((screen + pixels_per_meter * offset).round())
		if not Rect2i(Vector2i.ZERO, image.get_size()).has_point(point):
			return false
		var color := image.get_pixelv(point)
		var matches := color.r > 0.65 and color.b < 0.25 and (color.g < 0.25 if roof else color.g > 0.65)
		ok = ok and (matches == (absf(offset.x) < exterior))
		marker_samples += 1
	return ok

func finish(stats: Dictionary) -> void:
	stage = "done"
	if not visual:
		var csv := FileAccess.open("res://results/%s-frames.csv" % report_name, FileAccess.WRITE)
		if not csv:
			fail("Could not save individual frame measurements")
			return
		csv.store_line("frame,world_x_m,wall_interval_us,activation_us")
		for index in range(frame_intervals.size()):
			csv.store_line("%d,%.1f,%d,%d" % [index + 1, frame_positions[index], frame_intervals[index], frame_activation_us[index]])
		csv.close()
	frame_intervals.sort()
	var p99_ms := frame_intervals[ceili(0.99 * frame_intervals.size()) - 1] / 1000.0
	var after := memory()
	var final_scenes := resident_scenes()
	var cached := dense_textures_still_cached()
	var result := {
		"mode": report_name, "frames": frame_intervals.size(),
		"distance_m": END_X - START_X, "physics_speed_m_s": SPEED,
		"elapsed_s": (travel_end_us - travel_start_us) / 1e6, "origin_shifts_x_m": shifts,
		"p99_frame_ms": p99_ms, "max_activation_ms": max_activation_ms,
		"max_activation_x_m": max_activation_x, "frames_activation_over_2ms": activation_over_budget,
		"peak_residents": peak_residents, "peak_draw_calls": max_draw_calls,
		"before": before, "peak_dense_video_bytes": peak_bytes, "peak_dense_texture_bytes": peak_dense_texture_bytes,
		"after": after, "before_resident_cells": before_residents, "after_resident_cells": stats.resident_cells,
		"before_scene_batches": initial_scenes, "after_scene_batches": final_scenes,
		"dense_textures_still_cached": cached,
	}
	var passed: bool = shifts.size() >= 4 and peak_bytes > before.video_bytes and peak_dense_texture_bytes > before.texture_bytes
	if visual:
		result.merge({"readbacks": visual_frames, "marker_samples": marker_samples,
			"visible_fade_frames": fading_frames, "failed_visual_frames": bad_visual_frames,
			"road_failures": road_fails, "roof_failures": roof_fails, "first_bad": first_bad,
			"terrain_pixels_checked": terrain_pixels, "frames_with_terrain_holes": terrain_fails,
			"clear_color_positive_control": clear_color_detected,
			"shift_window_frames": shift_window_frames, "failed_shift_frames": bad_shift_frames})
		passed = passed and clear_color_detected and fading_frames > 0 and visual_frames == frame_intervals.size() and bad_visual_frames == 0 and shift_window_frames >= 4 * shifts.size()
	else:
		passed = passed and not initial_scenes.is_empty() and initial_scenes == final_scenes and cached == 0
		passed = passed and p99_ms <= 16.6 and max_activation_ms <= 2.0 and after.video_bytes <= before.video_bytes
	result.passed = passed
	if warm_district:
		result.warmup = {
			"cold_rural": cold, "peak_video_bytes": warm_peak, "after_eviction": before,
			"turn_x_m": WARMUP_TURN_X, "cold_to_final_video_bytes": int(after.video_bytes) - int(cold.video_bytes),
		}
		result.passed = result.passed and warm_peak > before.video_bytes and cold_scenes == initial_scenes
		result.passed = result.passed and cold.texture_bytes == before.texture_bytes and cold.buffer_bytes == before.buffer_bytes
	result.config = run_config
	result.settled_windows = settled_windows
	var result_file := FileAccess.open("res://results/%s.json" % run_config.mode, FileAccess.WRITE)
	if not result_file:
		fail("Could not save the run summary")
		return
	result_file.store_string(JSON.stringify(result, "\t") + "\n")
	result_file.close()
	print("GATE6 result ", JSON.stringify(result))
	get_tree().quit(0 if result.passed else 1)

func fail(reason: String) -> void:
	stage = "done"
	printerr("GATE6 error ", reason)
	get_tree().quit(2)
