extends Node3D

class ClockModule extends SimModule:
	var ticks := 0
	var seconds := 0.0

	func _on_tick(delta: float, _state: Object, _events: Object) -> void:
		ticks += 1
		seconds += delta

var module := ClockModule.new()
var module_rid: RID
var min_audio_scale := INF
var max_audio_scale := 0.0
var sample_count := 0
var feedback := PacketPeerUDP.new()
var terminal := TerminalDisplay3D.new()

func sample() -> Dictionary:
	return {
		"usec": Time.get_ticks_usec(),
		"physics_ticks": $Body.ticks,
		"body_x": $Body.integrated_x,
		"audio_seconds": $Audio.get_playback_position(),
		"clock_ticks": SandboxClock.get_sim_tick_index(),
		"module_ticks": module.ticks,
		"module_seconds": module.seconds,
		"ui_seconds": SandboxClock.get_ui_time_elapsed(),
		"drawn_frames": Engine.get_frames_drawn(),
	}

func _process(_delta: float) -> void:
	var scale := AudioServer.playback_speed_scale
	min_audio_scale = minf(min_audio_scale, scale)
	max_audio_scale = maxf(max_audio_scale, scale)
	sample_count += 1

func _ready() -> void:
	call_deferred("run")

func screen_pixel(terminal_pixel: Vector2) -> Vector2:
	var uv := terminal_pixel / Vector2(terminal.resolution)
	var point := Vector3((uv.x - 0.5) * terminal.screen_size.x, (0.5 - uv.y) * terminal.screen_size.y, 0)
	return $Camera.unproject_position(terminal.get_screen().to_global(point))

func capture_command_line() -> Image:
	var rect := terminal.get_command_line().get_global_rect()
	var pixels := Rect2i(Rect2(screen_pixel(rect.position), screen_pixel(rect.end) - screen_pixel(rect.position)))
	return get_viewport().get_texture().get_image().get_region(pixels)

func changed_pixels(first: Image, last: Image) -> int:
	var count := 0
	for y in first.get_height():
		for x in first.get_width():
			if first.get_pixel(x, y) != last.get_pixel(x, y):
				count += 1
	return count

func run() -> void:
	var mode := OS.get_cmdline_user_args()[0]
	if feedback.set_dest_address("127.0.0.1", int(OS.get_cmdline_user_args()[1])) != OK:
		get_tree().quit(1)
		return
	module.module_id = &"gate1.clock_probe"
	module.tick_rate_hz = 20.0
	module.thread_safe_tick = false
	module_rid = SimulationServer.register_module(module)
	SandboxClock.sim_time_scale = 50.0 if mode == "fast" else 0.0
	SandboxClock.sim_paused = mode == "paused"
	var init_started := Time.get_ticks_usec()
	add_child(terminal)
	var terminal_init_usec := Time.get_ticks_usec() - init_started

	var tone := AudioStreamWAV.new()
	tone.format = AudioStreamWAV.FORMAT_8_BITS
	tone.mix_rate = 8000
	var pcm := PackedByteArray()
	pcm.resize(tone.mix_rate * 16)
	for index in pcm.size():
		pcm[index] = int(24 * sin(TAU * 250 * index / tone.mix_rate)) & 255
	tone.data = pcm
	$Audio.stream = tone
	$Audio.play()
	await get_tree().create_timer(1.5).timeout
	if terminal.engage() != OK or not module_rid.is_valid():
		get_tree().quit(1)
		return
	await RenderingServer.frame_post_draw
	var initial_pixels := capture_command_line()
	terminal.get_command_line().text_changed.connect(on_text_changed)
	var before := sample()
	print("GATE1 ", JSON.stringify({"event": "ready", "window": DisplayServer.window_get_native_handle(DisplayServer.WINDOW_HANDLE)}))
	await get_tree().create_timer(7.0).timeout
	var after := sample()
	print("GATE1 ", JSON.stringify({
		"event": "finished", "mode": mode, "pid": OS.get_process_id(), "before": before, "after": after,
		"module_id": module.module_id, "body_state": $Body.backend,
		"physics_step_min": $Body.min_step, "physics_step_max": $Body.max_step,
		"sim_scale": SandboxClock.sim_time_scale, "sim_paused": SandboxClock.sim_paused,
		"audio_driver": AudioServer.get_driver_name(), "audio_device": AudioServer.output_device,
		"audio_playing": $Audio.playing, "player_pitch": $Audio.pitch_scale,
		"audio_scale_min": min_audio_scale, "audio_scale_max": max_audio_scale, "scale_samples": sample_count,
		"display": DisplayServer.get_name(), "renderer": RenderingServer.get_current_rendering_method(),
		"render_driver": RenderingServer.get_current_rendering_driver_name(), "gpu": RenderingServer.get_video_adapter_name(),
		"resolution": get_viewport().size, "version": Engine.get_version_info().string,
		"terminal_text": terminal.get_command_line().text, "terminal_engaged": terminal.is_engaged(),
		"terminal_first_attach_usec": terminal_init_usec,
		"command_region_changed_pixels": changed_pixels(initial_pixels, capture_command_line()),
	}))
	SimulationServer.unregister_module(module_rid)
	terminal.disengage()
	get_tree().quit()

func on_text_changed(value: String) -> void:
	feedback.put_packet(JSON.stringify({"event": "edited", "text": value}).to_utf8_buffer())
	await RenderingServer.frame_post_draw
	feedback.put_packet(JSON.stringify({"event": "drawn", "text": value, "frame": Engine.get_frames_drawn()}).to_utf8_buffer())
