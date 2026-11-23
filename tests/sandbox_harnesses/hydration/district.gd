extends Node3D

const Actor = preload("res://district_actor.gd")
const BLOCKS := 100
const VEHICLES := 4
const PEDESTRIANS := 16
const AGENTS := BLOCKS * (VEHICLES + PEDESTRIANS)
const POOL := {&"traffic": 24, &"pedestrian": 64}
const ROUTE_FRAMES := 720
const ROUTE_METERS := 1440.0

var bubble: Object
var sim: Object
var clock: Object
var evidence_path := "user://hydration-evidence.json"
var frame := 0
var previous_us := 0
var initial_created := 0
var initial_destroyed := 0
var previous_hydrations := 0
var previous_dehydrations := 0
var start_ticks := 0
var start_dispatched := 0
var start_awaited := 0
var witness := RID()
var witness_start_x := 0.0
var samples: Array[Dictionary] = []
var peak_claimed := {&"traffic": 0, &"pedestrian": 0}
var errors := PackedStringArray()


func _ready() -> void:
	var args := OS.get_cmdline_user_args()
	for i in args.size():
		if args[i] == "--evidence-path" and i + 1 < args.size():
			evidence_path = args[i + 1]
		elif args[i].begins_with("--evidence-path="):
			evidence_path = args[i].trim_prefix("--evidence-path=")
	evidence_path = ProjectSettings.globalize_path(evidence_path)
	bubble = Engine.get_singleton("HydrationBubble")
	sim = Engine.get_singleton("SimulationServer")
	clock = Engine.get_singleton("SandboxClock")
	if not bubble or not sim or not clock:
		abort_run("Requires sandbox_bubble, sandbox_sim and sandbox_core")
		return
	$Invoker/Camera.look_at(Vector3(12, 0, 0), Vector3.UP)
	var car := BoxMesh.new()
	car.size = Vector3(3.8, 1.4, 1.7)
	var car_shape := BoxShape3D.new()
	car_shape.size = car.size
	var pedestrian := CapsuleMesh.new()
	pedestrian.radius = 0.32
	pedestrian.height = 1.76
	var pedestrian_shape := CapsuleShape3D.new()
	pedestrian_shape.radius = pedestrian.radius
	pedestrian_shape.height = pedestrian.height
	if not configure(&"traffic", car, car_shape, Color.STEEL_BLUE, 42, 55, 180):
		return
	if not configure(&"pedestrian", pedestrian, pedestrian_shape, Color.GOLDENROD, 30, 42, 120):
		return
	for block in BLOCKS:
		for i in VEHICLES:
			var speed := 2.0 if i % 2 == 0 else -2.0
			var rid: RID = sim.create_agent(&"traffic", block * 16 + i * 4, 0.7, -4 + i * 2.6, Vector3(speed, 0, 0))
			if block == BLOCKS - 1 and i == 0:
				witness = rid
		for i in PEDESTRIANS:
			var side := 1 if i % 2 == 0 else -1
			sim.create_agent(&"pedestrian", block * 16 + i, 0.88, side * (7 + i % 3), Vector3(side * 0.6, 0, 0))
	clock.set_sim_tick_rate_hz(5.0)
	if bubble.start(self) != OK:
		abort_run("HydrationBubble.start failed")
		return
	initial_created = Actor.created
	initial_destroyed = Actor.destroyed
	if initial_created - initial_destroyed != POOL.traffic + POOL.pedestrian:
		abort_run("Prewarmed shell count differs from configured capacity")
		return
	var stats: Dictionary = bubble.get_stats()
	if not stats.has("last_update_usec") or not stats.has("last_transition_usec"):
		abort_run("HydrationBubble.get_stats lacks timing fields")
		return
	previous_us = Time.get_ticks_usec()


func configure(kind: StringName, mesh: Mesh, shape: Shape3D, tint: Color, enter: float, exit_radius: float, proxy: float) -> bool:
	var material := StandardMaterial3D.new()
	material.albedo_color = tint
	mesh.surface_set_material(0, material)
	var body := Actor.new()
	body.name = kind
	body.sync_to_physics = false
	var visual := MeshInstance3D.new()
	visual.mesh = mesh
	var collision := CollisionShape3D.new()
	collision.shape = shape
	var adapter := HydratableActor.new()
	adapter.name = "Hydratable"
	adapter.archetype_id = kind
	adapter.hydrate_radius = enter
	adapter.dehydrate_radius = exit_radius
	for child: Node in [visual, collision, adapter]:
		body.add_child(child)
		child.owner = body
	var scene := PackedScene.new()
	var err := scene.pack(body)
	body.free()
	if err != OK or bubble.configure_archetype(kind, scene, POOL[kind], mesh) != OK or bubble.set_radii(kind, enter, exit_radius, proxy) != OK:
		abort_run("Failed to configure archetype %s" % kind)
		return false
	return true


func _process(_delta: float) -> void:
	var now := Time.get_ticks_usec()
	var stats: Dictionary = bubble.get_stats()
	if frame == ROUTE_FRAMES - 1:
		start_ticks = clock.get_sim_tick_index()
		start_dispatched = sim.get_total_tasks_dispatched()
		start_awaited = sim.get_total_tasks_awaited()
		witness_start_x = sim.get_agent_local_position(witness).x
	if frame >= ROUTE_FRAMES:
		var hydrations := int(stats.hydrations) - previous_hydrations
		var dehydrations := int(stats.dehydrations) - previous_dehydrations
		var sample := {
			"frame": frame - ROUTE_FRAMES, "x_m": $Invoker.position.x,
			"frame_usec": now - previous_us, "last_update_usec": int(stats.last_update_usec),
			"last_transition_usec": int(stats.last_transition_usec), "tier0": int(stats.tier0),
			"tier1": int(stats.tier1), "tier2": int(stats.tier2), "hydrations": hydrations,
			"dehydrations": dehydrations,
		}
		for kind: StringName in POOL:
			var claimed := int(POOL[kind]) - int(stats.pools[kind].available)
			sample[kind] = claimed
			peak_claimed[kind] = maxi(peak_claimed[kind], claimed)
		samples.append(sample)
		if sample.tier0 + sample.tier1 + sample.tier2 != AGENTS or sample.traffic + sample.pedestrian != sample.tier2:
			record_error("Tier or pool counts contradict the active population")
		if hydrations < 0 or hydrations > 4 or dehydrations < 0 or dehydrations > 6:
			record_error("Per-frame transition count exceeds the scheduler caps")
	previous_us = now
	previous_hydrations = stats.hydrations
	previous_dehydrations = stats.dehydrations
	frame += 1
	if frame == 2 * ROUTE_FRAMES:
		finish()
		return
	var phase := float(frame % ROUTE_FRAMES) / ROUTE_FRAMES
	$Invoker.position.x = ROUTE_METERS * (1.0 - absf(2.0 * phase - 1.0))


func summarize(key: String) -> Dictionary:
	var values := PackedInt64Array()
	var total := 0
	var over_budget := 0
	for sample in samples:
		var value := int(sample[key])
		values.append(value)
		total += value
		if value > 1500:
			over_budget += 1
	values.sort()
	var result := {
		"mean": float(total) / values.size(), "p50": values[ceili(values.size() * 0.50) - 1],
		"p99": values[ceili(values.size() * 0.99) - 1], "max": values[-1],
	}
	if key != "frame_usec":
		result.frames_over_1500_usec = over_budget
	return result


func finish() -> void:
	set_process(false)
	var ticks := int(clock.get_sim_tick_index()) - start_ticks
	var dispatched := int(sim.get_total_tasks_dispatched()) - start_dispatched
	var awaited := int(sim.get_total_tasks_awaited()) - start_awaited
	var witness_displacement := float(sim.get_agent_local_position(witness).x) - witness_start_x
	if ticks <= 0 or dispatched <= 0 or awaited != dispatched:
		record_error("Expected completed clock-driven worker groups")
	if witness_displacement <= 0:
		record_error("The eastbound traffic witness failed to advance")
	if Actor.created != initial_created or Actor.destroyed != initial_destroyed:
		record_error("A pooled physics shell was created or destroyed after prewarm")
	if peak_claimed.traffic == 0 or peak_claimed.pedestrian == 0:
		record_error("Both archetypes must hydrate during the measured route")
	var totals := {"hydrations": 0, "dehydrations": 0}
	var proxy_frames := 0
	for sample in samples:
		totals.hydrations += sample.hydrations
		totals.dehydrations += sample.dehydrations
		if sample.tier1 > 0:
			proxy_frames += 1
	if totals.hydrations == 0 or totals.dehydrations == 0 or proxy_frames == 0:
		record_error("Expected hydration, dehydration and Tier 1 proxy activity")
	var functional_checks_passed := errors.is_empty()
	var update_times := summarize("last_update_usec")
	if update_times.frames_over_1500_usec > 0:
		record_error("Full hydration update exceeded 1,500 microseconds")
	var report := {
		"passed": errors.is_empty(), "functional_checks_passed": functional_checks_passed,
		"errors": errors, "engine": Engine.get_version_info(),
		"display": DisplayServer.get_name(), "adapter": RenderingServer.get_video_adapter_name(),
		"renderer": RenderingServer.get_current_rendering_method(), "agents": AGENTS,
		"warmup_frames": ROUTE_FRAMES, "measured_frames": samples.size(),
		"one_way_distance_m": ROUTE_METERS, "simulation_rate_hz": 5,
		"sim_ticks": ticks, "worker_groups_dispatched": dispatched, "worker_groups_awaited": awaited,
		"eastbound_vehicle_displacement_m": witness_displacement,
		"capacities": POOL, "peak_claimed": peak_claimed, "transitions": totals,
		"proxy_frames": proxy_frames, "shells_created_after_prewarm": Actor.created - initial_created,
		"shells_destroyed_after_prewarm": Actor.destroyed - initial_destroyed,
		"frame_usec": summarize("frame_usec"), "last_update_usec": update_times,
		"last_transition_usec": summarize("last_transition_usec"), "samples": samples,
	}
	var directory_error := DirAccess.make_dir_recursive_absolute(evidence_path.get_base_dir())
	if directory_error != OK:
		abort_run("Cannot create evidence directory: %s" % error_string(directory_error))
		return
	var file := FileAccess.open(evidence_path, FileAccess.WRITE)
	if not file:
		abort_run("Cannot open evidence file: %s" % error_string(FileAccess.get_open_error()))
		return
	var stored := file.store_string(JSON.stringify(report, "\t") + "\n")
	file.flush()
	var write_error := file.get_error()
	file.close()
	if not stored or write_error != OK:
		abort_run("Cannot write evidence file: %s" % error_string(write_error))
		return
	print("Gate 7 district: ", evidence_path, " errors=", errors, " update_usec=", report.last_update_usec)
	get_tree().quit(0 if errors.is_empty() else 1)


func record_error(message: String) -> void:
	if not errors.has(message):
		errors.append(message)


func abort_run(message: String) -> void:
	set_process(false)
	push_error(message)
	get_tree().quit(1)
