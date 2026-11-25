class_name BlackoutManager
extends Node3D

## Orchestrates the Blackout Protocol Emergency Dispatch & Disaster Simulator Showcase.
## Demonstrates:
## 1. Three Concurrent SimModules (WeatherStormSim, FireSpreadSim, EvacuationTrafficSim)
## 2. High-Speed (90 m/s) Helicopter Flight with 3.5s Velocity-Predictive Lookahead Streaming
## 3. Time-Sliced <= 2.0 ms Cell Activation & Multiple 2048m OriginShifter Shifts
## 4. 400-Agent Evacuation & Fire Brigade HydrationBubble
## 5. In-Cockpit Diegetic 3D Terminal + Rooftop Emergency Dispatch Consoles

@export var weather_module: Resource
@export var fire_module: Resource
@export var evac_module: Resource
@export var blackout_mission: Resource
@export var partition_manifest: Resource
@export var fire_truck_prefab: PackedScene
@export var evac_sedan_prefab: PackedScene
@export var fire_truck_proxy_mesh: Mesh
@export var evac_sedan_proxy_mesh: Mesh

var weather_rid: RID
var fire_rid: RID
var evac_rid: RID
var hud_label: RichTextLabel
var chopper_rig: CharacterBody3D
var fire_beacons: Array[OmniLight3D] = []
var origin_shifts_observed: int = 0
var cells_activated_count: int = 0


func _ready() -> void:
	OriginShifter.register_root(self)
	chopper_rig = get_node_or_null("RescueChopper") as CharacterBody3D

	# 1. Register Emergency Dispatch CommandBus Commands
	_register_domain_commands()

	# 2. Register 3 Concurrent SimModules & Enable Rewind Ring Buffer
	SimulationServer.set_rewind_capture_enabled(true)
	SimulationServer.set_snapshot_interval_ticks(10)
	if weather_module is SimModule:
		weather_rid = SimulationServer.register_module(weather_module)
	if fire_module is SimModule:
		fire_rid = SimulationServer.register_module(fire_module)
	if evac_module is SimModule:
		evac_rid = SimulationServer.register_module(evac_module)

	# 3. Enable Coastal City WorldPartitionManifest Streaming (2.0 ms Time-Sliced Budget)
	if partition_manifest is WorldPartitionManifest:
		StreamingServer.set_budget_ms(2.0)
		if StreamingServer.set_manifest(partition_manifest) == OK:
			StreamingServer.set_enabled(true)

	# 4. Configure HydrationBubble & Spawn 400 Evacuation Sedans + Fire Trucks
	_setup_hydration_bubble()

	# 5. Register & Start the Blackout Protocol Mission
	if blackout_mission is MissionData:
		MissionRunner.register_mission(blackout_mission)
		MissionRunner.start_mission(blackout_mission.mission_id)

	for light in find_children("*", "OmniLight3D", true, false):
		if light.is_in_group(&"fire_hazard_light"):
			fire_beacons.append(light as OmniLight3D)

	hud_label = get_node_or_null("HUD/Panel/Margin/TelemetryLabel") as RichTextLabel

	EventBus.subscribe(&"origin_shifted", _on_origin_shifted)
	EventBus.subscribe(&"stream_cell_activated", _on_stream_cell_activated)
	EventBus.subscribe(&"fire.all_contained", _on_disaster_contained)

	if "--verify-blackout" in OS.get_cmdline_user_args():
		_run_automated_showcase_verification.call_deferred()


func _exit_tree() -> void:
	EventBus.unsubscribe(&"origin_shifted", _on_origin_shifted)
	EventBus.unsubscribe(&"stream_cell_activated", _on_stream_cell_activated)
	EventBus.unsubscribe(&"fire.all_contained", _on_disaster_contained)
	HydrationBubble.stop()
	StreamingServer.set_enabled(false)
	for rid in [weather_rid, fire_rid, evac_rid]:
		if rid.is_valid():
			SimulationServer.unregister_module(rid)
	OriginShifter.unregister_root(self)
	_unregister_domain_commands()


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode == KEY_F1:
			_teleport_chopper(&"MarkerHelipadHQ")
		elif event.keycode == KEY_F2:
			_teleport_chopper(&"MarkerRefineryFire")
		elif event.keycode == KEY_F3:
			_teleport_chopper(&"MarkerSeawallControl")
		elif event.keycode == KEY_F4:
			var next_scale: float = 20.0 if SandboxClock.get_sim_time_scale() < 5.0 else 1.0
			SandboxClock.set_sim_time_scale(next_scale)
		elif event.keycode == KEY_F5:
			SimulationServer.rewind(40)


func _process(_delta: float) -> void:
	var hazard_pct: float = WorldState.get_float(&"fire.city_hazard_pct", 78.0)
	for beacon in fire_beacons:
		if is_instance_valid(beacon):
			beacon.light_energy = clampf(hazard_pct / 20.0, 0.4, 4.5)
			beacon.light_color = Color(0.15, 0.95, 0.45) if hazard_pct <= 15.0 else Color(1.0, 0.28, 0.08)
	_update_hud()


func _register_domain_commands() -> void:
	CommandBus.register_command(
		&"dispatch.status",
		CommandBus.PERMISSION_GUEST,
		[],
		_cmd_dispatch_status,
		"Displays live coastal storm, fire hazard %, evacuation progress, and streaming telemetry."
	)
	CommandBus.register_command(
		&"dispatch.radar_sweep",
		CommandBus.PERMISSION_USER,
		[],
		_cmd_dispatch_radar_sweep,
		"Links Rescue-1 airborne Doppler & thermal radar with Coastal Dispatch HQ."
	)
	CommandBus.register_command(
		&"dispatch.fire_trucks",
		CommandBus.PERMISSION_USER,
		[TYPE_BOOL],
		_cmd_dispatch_fire_trucks,
		"Deploys or recalls ground foam-suppression fire brigades (dispatch.fire_trucks true|false)."
	)
	CommandBus.register_command(
		&"dispatch.airdrop",
		CommandBus.PERMISSION_USER,
		[],
		_cmd_dispatch_airdrop,
		"Executes an aerial fire-retardant drop from Rescue-1 over the refinery sector."
	)
	CommandBus.register_command(
		&"dispatch.contraflow",
		CommandBus.PERMISSION_ADMIN,
		[TYPE_BOOL],
		_cmd_dispatch_contraflow,
		"[ADMIN] Reverses all coastal highway lanes for high-speed civilian evacuation contraflow."
	)
	CommandBus.register_command(
		&"weather.floodgate",
		CommandBus.PERMISSION_ADMIN,
		[TYPE_BOOL],
		_cmd_weather_floodgate,
		"[ADMIN] Opens or closes harbor diversion floodgates to drain storm surge."
	)


func _unregister_domain_commands() -> void:
	for cmd in [&"dispatch.status", &"dispatch.radar_sweep", &"dispatch.fire_trucks", &"dispatch.airdrop", &"dispatch.contraflow", &"weather.floodgate"]:
		if CommandBus.has_command(cmd):
			CommandBus.unregister_command(cmd)


func _cmd_dispatch_status() -> String:
	var wind: float = WorldState.get_float(&"weather.wind_speed_knots", 58.0)
	var flood: float = WorldState.get_float(&"weather.flood_level_m", 3.4)
	var hazard: float = WorldState.get_float(&"fire.city_hazard_pct", 78.0)
	var evac: float = WorldState.get_float(&"evac.civilians_evacuated_pct", 22.0)
	return "BLACKOUT DISPATCH | Wind: %.0f kt | Surge: %.2f m | Fire Hazard: %.1f%% | Evacuated: %.1f%%" % [
		wind, flood, hazard, evac
	]


func _cmd_dispatch_radar_sweep() -> String:
	WorldState.set_bool(&"mission.radar_linked", true)
	EventBus.emit(&"evac.radar_swept", {"unit": "rescue_chopper"})
	return "DOPPLER & FLIR RADAR LINKED: 6 burning blocks & 3.4m harbor surge detected. Deploy Fire Brigades / Aerial Drop and open Highway Contraflow + Seawall Floodgates."


func _cmd_dispatch_fire_trucks(deploy: bool) -> String:
	WorldState.set_bool(&"fire.trucks_dispatched", deploy)
	if deploy:
		EventBus.emit(&"fire.trucks_deployed", {"units": 80})
	return "GROUND FIRE BRIGADES: %s" % ("DEPLOYED (80 Foam Units Active)" if deploy else "STANDBY")


func _cmd_dispatch_airdrop() -> String:
	WorldState.set_bool(&"fire.airdrop_deployed", true)
	EventBus.emit(&"fire.airdrop_executed", {"sector": "refinery"})
	return "RESCUE-1 AERIAL RETARDANT DROP EXECUTED: Refinery thermal signature collapsing."


func _cmd_dispatch_contraflow(active: bool) -> String:
	WorldState.set_bool(&"evac.contraflow_active", active)
	if active:
		EventBus.emit(&"evac.contraflow_enabled", {"speed_mps": 28.0})
	return "COASTAL HIGHWAY CONTRAFLOW: %s" % ("ALL LANES OUTBOUND (28 m/s Evacuation)" if active else "STANDARD")


func _cmd_weather_floodgate(open: bool) -> String:
	WorldState.set_bool(&"weather.seawall_gates_open", open)
	if open:
		EventBus.emit(&"weather.floodgates_opened", {})
	return "HARBOR SEAWALL DIVERSION GATES: %s" % ("OPEN — Draining storm surge to 1.2m." if open else "CLOSED")


func _teleport_chopper(marker_name: StringName) -> void:
	var marker := find_child(String(marker_name), true, false) as Node3D
	if marker and chopper_rig:
		var actor := chopper_rig.get_node("SandboxActor") as SandboxActor
		actor.request_teleport(marker.global_transform)


func _setup_hydration_bubble() -> void:
	if fire_truck_prefab and fire_truck_proxy_mesh:
		HydrationBubble.configure_archetype(&"fire_truck", fire_truck_prefab, 16, fire_truck_proxy_mesh, HydrationBubble.RECYCLE_FARTHEST_UNPINNED)
		HydrationBubble.set_radii(&"fire_truck", 130.0, 160.0, 480.0)
	if evac_sedan_prefab and evac_sedan_proxy_mesh:
		HydrationBubble.configure_archetype(&"evac_sedan", evac_sedan_prefab, 32, evac_sedan_proxy_mesh, HydrationBubble.RECYCLE_FARTHEST_UNPINNED)
		HydrationBubble.set_radii(&"evac_sedan", 110.0, 140.0, 420.0)
	HydrationBubble.start(self)

	# Spawn 80 Fire Trucks and 320 Civilian Evacuation Sedans along the 5km coastal highway
	if SimulationServer.get_agent_count() == 0:
		for i in 80:
			var x: float = -200.0 + float(i) * 55.0
			var z: float = -8.0 if (i % 2 == 0) else 8.0
			SimulationServer.create_agent(&"fire_truck", x, 0.9, z, Vector3(14.0, 0.0, 0.0), PI * 0.5, 100.0, 0)
		for i in 320:
			var x: float = -250.0 + float(i) * 14.0
			var z: float = -16.0 if (i % 2 == 0) else 16.0
			SimulationServer.create_agent(&"evac_sedan", x, 0.6, z, Vector3(11.0, 0.0, 0.0), PI * 0.5, 100.0, 0)


func _on_origin_shifted(_payload: Dictionary) -> void:
	origin_shifts_observed += 1


func _on_stream_cell_activated(_payload: Dictionary) -> void:
	cells_activated_count += 1


func _on_disaster_contained(_payload: Dictionary) -> void:
	var anim := get_node_or_null("CutsceneAnimationPlayer") as AnimationPlayer
	if anim and anim.has_animation(&"city_saved_cutscene"):
		anim.play(&"city_saved_cutscene")
		anim.advance(0.25)


func _update_hud() -> void:
	if not is_instance_valid(hud_label):
		return
	var wind: float = WorldState.get_float(&"weather.wind_speed_knots", 58.0)
	var flood: float = WorldState.get_float(&"weather.flood_level_m", 3.4)
	var hazard: float = WorldState.get_float(&"fire.city_hazard_pct", 78.0)
	var blocks: int = WorldState.get_int(&"fire.burning_blocks", 6)
	var evac: float = WorldState.get_float(&"evac.civilians_evacuated_pct", 22.0)
	var b_stats: Dictionary = HydrationBubble.get_stats()
	var s_stats: Dictionary = StreamingServer.get_stats()
	var origin: PackedFloat64Array = OriginShifter.get_origin_64()
	var invoker := chopper_rig.get_node("StreamingInvoker") as StreamingInvoker
	var vel_mps: float = invoker.get_velocity().length() if invoker else 0.0
	var active_objs: PackedStringArray = MissionRunner.get_active_objectives(&"blackout_emergency")
	var m_status: int = MissionRunner.get_mission_status(&"blackout_emergency")
	var status_str: String = "COMPLETED" if m_status == MissionRunner.STATUS_COMPLETED else ("ACTIVE: " + ", ".join(active_objs))

	hud_label.text = (
		"[b]BLACKOUT PROTOCOL — COASTAL DISASTER & AIRBORNE TELEMETRY[/b]\n" +
		"Mission [blackout_emergency]: %s\n" % status_str +
		"Storm: %.0f kt Wind | Harbor Surge: %.2f m | Fire Hazard: %.1f%% (%d Blocks) | Evacuated: %.1f%%\n" % [
			wind, flood, hazard, blocks, evac
		] +
		"Rescue-1 Speed: %.1f m/s (Lookahead 3.5s) | Streamed Cells: %d (Act: %.2f ms <= %.1f ms) | Origin64: [%.0f, %.0f, %.0f] | Hydration T0/T1/T2: %d/%d/%d\n" % [
			vel_mps,
			int(s_stats.get("resident_cells", 0)),
			float(s_stats.get("activation_ms", 0.0)),
			float(s_stats.get("budget_ms", 2.0)),
			origin[0], origin[1], origin[2],
			int(b_stats.get("tier0", 0)),
			int(b_stats.get("tier1", 0)),
			int(b_stats.get("tier2", 0))
		] +
		"[WASD+QE+Shift] Fly Rescue-1 (95 m/s) | [Click Cockpit Console] Dispatch CLI | [F1] HQ [F2] Refinery [F3] Seawall [F4] 20x Sim [F5] Rewind"
	)


func _run_automated_showcase_verification() -> void:
	print("=== BLACKOUT PROTOCOL AUTOMATED VERIFICATION ===")
	var cockpit_term := find_child("ChopperCockpitTerminal", true, false) as TerminalDisplay3D
	var hq_term := find_child("DispatchHQTerminal", true, false) as TerminalDisplay3D
	var seawall_term := find_child("SeawallTerminal", true, false) as TerminalDisplay3D
	var chopper_cam := chopper_rig.get_node("Camera3D") as Camera3D
	var invoker := chopper_rig.get_node("StreamingInvoker") as StreamingInvoker

	for f in 12:
		await get_tree().process_frame

	var b_stats: Dictionary = HydrationBubble.get_stats()
	var s_stats: Dictionary = StreamingServer.get_stats()
	var total_agents: int = SimulationServer.get_agent_count()
	var mod_count: int = SimulationServer.get_module_count()
	print("1. Modules active: ", mod_count, " | Evacuation Fleet: ", total_agents, " | Tier 1 proxies: ", b_stats.get("tier1", 0), " | Tier 2 hydrated: ", b_stats.get("tier2", 0), " | Resident cells: ", s_stats.get("resident_cells", 0))

	# Step 1: Execute Doppler & FLIR Radar Sweep from the In-Cockpit Helicopter Terminal
	var radar_res: Dictionary = cockpit_term.execute_command("dispatch.radar_sweep")
	print("2. In-Cockpit dispatch.radar_sweep ok=", radar_res.get("ok"))
	for f in 4:
		await get_tree().process_frame

	var active_branches: PackedStringArray = MissionRunner.get_active_objectives(&"blackout_emergency")
	print("3. Active disaster response branches: ", active_branches)

	# Step 2: Fly Rescue-1 at 90 m/s across the coastal city (> 4,500m across 2 OriginShifter thresholds)
	# verifying predictive lookahead streaming and <= 2.0 ms activation budget
	var peak_activation_ms: float = 0.0
	for frame_idx in 60:
		chopper_rig.global_position += Vector3(76.0, 0.0, 0.0)
		await get_tree().physics_frame
		await get_tree().process_frame
		var st: Dictionary = StreamingServer.get_stats()
		peak_activation_ms = maxf(peak_activation_ms, float(st.get("activation_ms", 0.0)))

	var origin_after_flight: PackedFloat64Array = OriginShifter.get_origin_64()
	var sampled_vel: float = invoker.get_velocity().length()
	print("4. 90 m/s Helicopter Flight: sampled_vel=", sampled_vel, " m/s | origin_shifts=", origin_shifts_observed, " | Origin64=[", origin_after_flight[0], ", ", origin_after_flight[1], ", ", origin_after_flight[2], "] | Peak activation_ms=", peak_activation_ms)

	# Step 3: Snapshot, then dispatch Fire Trucks, Aerial Drop, Highway Contraflow, and Seawall Floodgates
	SimulationServer.capture_rewind_snapshot_now()
	var pre_dispatch_tick: int = SandboxClock.get_sim_tick_index()
	var truck_res: Dictionary = cockpit_term.execute_command("dispatch.fire_trucks true")
	var drop_res: Dictionary = cockpit_term.execute_command("dispatch.airdrop")
	var contra_res: Dictionary = hq_term.execute_command("dispatch.contraflow true")
	var gate_res: Dictionary = seawall_term.execute_command("weather.floodgate true")
	print("5. Emergency Directives Dispatched: trucks=", truck_res.get("ok"), " airdrop=", drop_res.get("ok"), " contraflow=", contra_res.get("ok"), " floodgate=", gate_res.get("ok"))

	# Step 4: Step simulation 50 ticks so all 3 SimModules converge (fire <= 15%, flood <= 1.8m, evac >= 90%)
	SandboxClock.step_immediate(50)
	for f in 12:
		await get_tree().process_frame

	var mission_completed: bool = MissionRunner.get_mission_status(&"blackout_emergency") == MissionRunner.STATUS_COMPLETED
	var hazard_final: float = WorldState.get_float(&"fire.city_hazard_pct")
	var evac_final: float = WorldState.get_float(&"evac.civilians_evacuated_pct")
	var flood_final: float = WorldState.get_float(&"weather.flood_level_m")
	var flir_zoom_ok: bool = chopper_cam.fov < 60.0
	print("6. Disaster Contained: mission_completed=", mission_completed, " | Fire Hazard=", hazard_final, "% | Evacuated=", evac_final, "% | Flood=", flood_final, "m | FLIR Cutscene FOV=", chopper_cam.fov)

	# Step 5: Test Deterministic Rewind across all 3 SimModules
	var rewind_ticks: int = max(1, int(SandboxClock.get_sim_tick_index() - pre_dispatch_tick))
	var rw_err: Error = SimulationServer.rewind(rewind_ticks)
	var hazard_rewound: float = WorldState.get_float(&"fire.city_hazard_pct")
	print("7. Multi-Module Rewind (", rewind_ticks, " ticks): err=", rw_err, " | Rewound Fire Hazard=", hazard_rewound, "%")

	var all_passed: bool = (
		mod_count == 3 and
		total_agents == 400 and
		int(b_stats.get("tier2", 0)) > 0 and
		int(b_stats.get("tier1", 0)) > 0 and
		int(s_stats.get("resident_cells", 0)) > 0 and
		bool(radar_res.get("ok", false)) and
		active_branches.size() >= 2 and
		origin_shifts_observed >= 2 and
		origin_after_flight[0] >= 4096.0 and
		peak_activation_ms <= 2.0 and
		mission_completed and
		hazard_final <= 15.0 and
		evac_final >= 90.0 and
		flir_zoom_ok and
		rw_err == OK and
		hazard_rewound > 50.0
	)
	print("=== BLACKOUT PROTOCOL VERIFICATION: ", "PASS" if all_passed else "FAIL", " ===")
	get_tree().quit(0 if all_passed else 1)
