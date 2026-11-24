class_name MeridianManager
extends Node3D

## Orchestrates the Outpost Meridian Dual-Perspective (1st-Person + Orbital RTS) Showcase.
## Demonstrates:
## 1. Seamless Perspective / Domain Switching (1st-Person Commander Rig <-> Orbital RTS Rig)
## 2. 50x Multi-Clock Fast-Forward (SandboxClock.set_sim_time_scale(50.0))
## 3. 16km Crater World Partition Streaming + Multiple 2048m OriginShifter Shifts
## 4. 300-Agent HydrationBubble Mining Fleet (Ore Haulers & Survey Drones)
## 5. Predictive Telemetry Rewind Console (sim.rewind)

@export var colony_module: Resource
@export var fleet_module: Resource
@export var meridian_mission: Resource
@export var partition_manifest: Resource
@export var hauler_prefab: PackedScene
@export var drone_prefab: PackedScene
@export var hauler_proxy_mesh: Mesh
@export var drone_proxy_mesh: Mesh

var colony_module_rid: RID
var fleet_module_rid: RID
var hud_label: RichTextLabel
var dome_beacon: OmniLight3D
var fp_rig: CharacterBody3D
var rts_rig: Node3D
var is_rts_mode: bool = false
var origin_shifts_observed: int = 0


func _ready() -> void:
	OriginShifter.register_root(self)
	fp_rig = get_node_or_null("CommanderFP") as CharacterBody3D
	rts_rig = get_node_or_null("OrbitalRTS") as Node3D

	# 1. Register Custom Colony & Orbital CommandBus Commands
	_register_domain_commands()

	# 2. Start SimulationServer Modules & Rewind Ring Buffer
	SimulationServer.set_rewind_capture_enabled(true)
	SimulationServer.set_snapshot_interval_ticks(10)
	if colony_module is SimModule:
		colony_module_rid = SimulationServer.register_module(colony_module)
	if fleet_module is SimModule:
		fleet_module_rid = SimulationServer.register_module(fleet_module)

	# 3. Enable 16km Crater WorldPartitionManifest Streaming
	if partition_manifest is WorldPartitionManifest:
		if StreamingServer.set_manifest(partition_manifest) == OK:
			StreamingServer.set_enabled(true)

	# 4. Configure HydrationBubble Pools & Spawn 300 Crater Mining Units
	_setup_hydration_bubble()

	# 5. Register & Start the Outpost Meridian Mission
	if meridian_mission is MissionData:
		MissionRunner.register_mission(meridian_mission)
		MissionRunner.start_mission(meridian_mission.mission_id)

	dome_beacon = find_child("DomeBeacon", true, false) as OmniLight3D
	hud_label = get_node_or_null("HUD/Panel/Margin/TelemetryLabel") as RichTextLabel

	EventBus.subscribe(&"origin_shifted", _on_origin_shifted)
	EventBus.subscribe(&"colony.quota_reached", _on_quota_reached)

	if "--verify-meridian" in OS.get_cmdline_user_args():
		_run_automated_showcase_verification.call_deferred()


func _exit_tree() -> void:
	EventBus.unsubscribe(&"origin_shifted", _on_origin_shifted)
	EventBus.unsubscribe(&"colony.quota_reached", _on_quota_reached)
	HydrationBubble.stop()
	StreamingServer.set_enabled(false)
	if colony_module_rid.is_valid():
		SimulationServer.unregister_module(colony_module_rid)
	if fleet_module_rid.is_valid():
		SimulationServer.unregister_module(fleet_module_rid)
	OriginShifter.unregister_root(self)
	_unregister_domain_commands()


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode == KEY_TAB:
			_set_view_mode("rts" if not is_rts_mode else "fp")
		elif event.keycode == KEY_F1:
			_teleport_active_rig(&"MarkerCommandDeck")
		elif event.keycode == KEY_F2:
			_teleport_active_rig(&"MarkerScrubberBay")
		elif event.keycode == KEY_F3:
			_teleport_active_rig(&"MarkerRoverGarage")
		elif event.keycode == KEY_F4:
			var next_scale: float = 50.0 if SandboxClock.get_sim_time_scale() < 10.0 else 1.0
			SandboxClock.set_sim_time_scale(next_scale)
		elif event.keycode == KEY_F5:
			SimulationServer.rewind(50)


func _process(_delta: float) -> void:
	if is_instance_valid(dome_beacon):
		var energy: float = WorldState.get_float(&"colony.dome_beacon_energy", 0.5)
		var quota_ok: bool = WorldState.get_float(&"colony.o2_pressure_kpa", 68.0) >= 95.0 and WorldState.get_float(&"colony.refined_ore_tons", 45.0) >= 250.0
		dome_beacon.light_energy = energy * 2.5
		dome_beacon.light_color = Color(0.2, 1.0, 0.55) if quota_ok else Color(1.0, 0.55, 0.15)
	_update_hud()


func _register_domain_commands() -> void:
	CommandBus.register_command(
		&"colony.status",
		CommandBus.PERMISSION_GUEST,
		[],
		_cmd_colony_status,
		"Displays Outpost Meridian O2 pressure, battery MWh, ore tonnage, and 64-bit world origin."
	)
	CommandBus.register_command(
		&"colony.link_telemetry",
		CommandBus.PERMISSION_USER,
		[],
		_cmd_colony_link_telemetry,
		"Synchronizes the Habitat Command Deck with orbital crater telemetry."
	)
	CommandBus.register_command(
		&"colony.scrubber_boost",
		CommandBus.PERMISSION_USER,
		[TYPE_BOOL],
		_cmd_colony_scrubber_boost,
		"Enables or disables atmospheric O2 scrubber overdrive (colony.scrubber_boost true|false)."
	)
	CommandBus.register_command(
		&"fleet.overclock",
		CommandBus.PERMISSION_ADMIN,
		[TYPE_BOOL],
		_cmd_fleet_overclock,
		"[ADMIN] Overclocks the 300 autonomous mining haulers across Meridian Crater."
	)
	CommandBus.register_command(
		&"view.mode",
		CommandBus.PERMISSION_USER,
		[TYPE_STRING],
		_cmd_view_mode,
		"Switches active perspective between 1st-Person Commander ('fp') and Orbital RTS ('rts')."
	)


func _unregister_domain_commands() -> void:
	for cmd in [&"colony.status", &"colony.link_telemetry", &"colony.scrubber_boost", &"fleet.overclock", &"view.mode"]:
		if CommandBus.has_command(cmd):
			CommandBus.unregister_command(cmd)


func _cmd_colony_status() -> String:
	var o2: float = WorldState.get_float(&"colony.o2_pressure_kpa", 68.0)
	var bat: float = WorldState.get_float(&"colony.battery_mwh", 140.0)
	var ore: float = WorldState.get_float(&"colony.refined_ore_tons", 45.0)
	var origin: PackedFloat64Array = OriginShifter.get_origin_64()
	return "OUTPOST MERIDIAN | O2: %.1f kPa | Battery: %.1f MWh | Refined Ore: %.1f t | Origin64: [%.0f, %.0f, %.0f] | Perspective: %s" % [
		o2, bat, ore, origin[0], origin[1], origin[2], "ORBITAL RTS" if is_rts_mode else "1ST-PERSON"
	]


func _cmd_colony_link_telemetry() -> String:
	WorldState.set_bool(&"mission.telemetry_linked", true)
	EventBus.emit(&"colony.telemetry_linked", {"crater": "meridian"})
	return "ORBITAL TELEMETRY LINKED: 300 mining units tracked. Boost O2 Scrubbers [F2] or Overclock Hauler Fleet at Orbital Command Console [F1], then fast-forward SimClock (sim.speed 50)."


func _cmd_colony_scrubber_boost(enabled: bool) -> String:
	WorldState.set_bool(&"colony.scrubber_overdrive", enabled)
	if enabled:
		EventBus.emit(&"colony.scrubber_boosted", {"overdrive": true})
	return "ATMOSPHERIC O2 SCRUBBERS: %s" % ("OVERDRIVE ACTIVE (+4.2 kPa/s)" if enabled else "STANDARD (+0.35 kPa/s)")


func _cmd_fleet_overclock(enabled: bool) -> String:
	WorldState.set_bool(&"fleet.convoy_overclock", enabled)
	if enabled:
		EventBus.emit(&"colony.convoy_recalled", {"speed_mps": 24.0})
	return "MINING HAULER FLEET: %s" % ("CONVOY OVERCLOCK (24 m/s, +28 tons/s)" if enabled else "NORMAL (9 m/s)")


func _cmd_view_mode(mode: String) -> String:
	var clean := mode.strip_edges().to_lower()
	if clean != "fp" and clean != "rts":
		return "Usage: view.mode fp | rts"
	_set_view_mode(clean)
	return "ACTIVE PERSPECTIVE SWITCHED TO: %s" % ("ORBITAL RTS COMMAND" if is_rts_mode else "1ST-PERSON COMMANDER")


func _set_view_mode(mode: String) -> void:
	is_rts_mode = (mode == "rts")
	if fp_rig and rts_rig:
		var fp_cam := fp_rig.get_node("Camera3D") as Camera3D
		var rts_cam := rts_rig.get_node("Camera3D") as Camera3D
		if is_rts_mode:
			rts_rig.global_position = Vector3(fp_rig.global_position.x, 55.0, fp_rig.global_position.z)
			rts_rig.reset_physics_interpolation()
			rts_cam.make_current()
		else:
			fp_cam.make_current()

		# Rebind all diegetic 3D terminals to lock whichever SandboxActor rig is currently active!
		var active_actor_path := NodePath("../OrbitalRTS/SandboxActor") if is_rts_mode else NodePath("../CommanderFP/SandboxActor")
		for term in find_children("*", "TerminalDisplay3D", true, false):
			(term as TerminalDisplay3D).actor_path = active_actor_path


func _teleport_active_rig(marker_name: StringName) -> void:
	var marker := find_child(String(marker_name), true, false) as Node3D
	if not marker:
		return
	var active_actor: SandboxActor = (rts_rig.get_node("SandboxActor") if is_rts_mode else fp_rig.get_node("SandboxActor")) as SandboxActor
	if active_actor:
		active_actor.request_teleport(marker.global_transform)


func _setup_hydration_bubble() -> void:
	if hauler_prefab and hauler_proxy_mesh:
		HydrationBubble.configure_archetype(&"ore_hauler", hauler_prefab, 20, hauler_proxy_mesh, HydrationBubble.RECYCLE_FARTHEST_UNPINNED)
		HydrationBubble.set_radii(&"ore_hauler", 110.0, 140.0, 450.0)
	if drone_prefab and drone_proxy_mesh:
		HydrationBubble.configure_archetype(&"survey_drone", drone_prefab, 25, drone_proxy_mesh, HydrationBubble.KEEP_TIER1_PROXY)
		HydrationBubble.set_radii(&"survey_drone", 80.0, 105.0, 320.0)
	HydrationBubble.start(self)

	# Spawn 120 Ore Haulers and 180 Survey Drones along the 6km crater mining highway
	if SimulationServer.get_agent_count() == 0:
		for i in 120:
			var x: float = -300.0 + float(i) * 45.0
			var z: float = 12.0 if (i % 2 == 0) else -12.0
			var vel_x: float = 10.0 if (i % 2 == 0) else -10.0
			var rot_y: float = PI * 0.5 if (i % 2 == 0) else -PI * 0.5
			SimulationServer.create_agent(&"ore_hauler", x, 0.9, z, Vector3(vel_x, 0.0, 0.0), rot_y, 100.0, 0)
		for i in 180:
			var x: float = -300.0 + float(i) * 30.0
			var z: float = 24.0 if (i % 2 == 0) else -24.0
			var vel_x: float = 14.0 if (i % 2 == 0) else -14.0
			var rot_y: float = PI * 0.5 if (i % 2 == 0) else -PI * 0.5
			SimulationServer.create_agent(&"survey_drone", x, 3.5, z, Vector3(vel_x, 0.0, 0.0), rot_y, 100.0, 0)


func _on_origin_shifted(_payload: Dictionary) -> void:
	origin_shifts_observed += 1
	EventBus.emit(&"fleet.origin_crossed", {"shifts": origin_shifts_observed})


func _on_quota_reached(_payload: Dictionary) -> void:
	var anim := get_node_or_null("CutsceneAnimationPlayer") as AnimationPlayer
	if anim and anim.has_animation(&"colony_quota_cutscene"):
		anim.play(&"colony_quota_cutscene")
		anim.advance(0.25)


func _update_hud() -> void:
	if not is_instance_valid(hud_label):
		return
	var o2: float = WorldState.get_float(&"colony.o2_pressure_kpa", 68.0)
	var bat: float = WorldState.get_float(&"colony.battery_mwh", 140.0)
	var ore: float = WorldState.get_float(&"colony.refined_ore_tons", 45.0)
	var b_stats: Dictionary = HydrationBubble.get_stats()
	var s_stats: Dictionary = StreamingServer.get_stats()
	var origin: PackedFloat64Array = OriginShifter.get_origin_64()
	var active_objs: PackedStringArray = MissionRunner.get_active_objectives(&"meridian_expansion")
	var m_status: int = MissionRunner.get_mission_status(&"meridian_expansion")
	var status_str: String = "COMPLETED" if m_status == MissionRunner.STATUS_COMPLETED else ("ACTIVE: " + ", ".join(active_objs))

	hud_label.text = (
		"[b]OUTPOST MERIDIAN — DUAL-PERSPECTIVE COLONY & CRATER TELEMETRY[/b]\n" +
		"Perspective: [color=#55eeff]%s[/color] ([Tab] Swap 1st-Person <-> Orbital RTS) | Mission: %s\n" % [
			"ORBITAL RTS (Orthographic)" if is_rts_mode else "1ST-PERSON COMMANDER (Perspective)",
			status_str
		] +
		"Atmosphere O2: %.1f / 95.0 kPa | Refined Ore: %.1f / 250.0 t | Solar Reserve: %.1f MWh\n" % [o2, ore, bat] +
		"SimClock: %.1fx (Tick #%d) | Origin64: [%.0f, %.0f, %.0f] (Shifts: %d) | Hydration T0/T1/T2: %d/%d/%d | Cells: %d\n" % [
			SandboxClock.get_sim_time_scale(),
			SandboxClock.get_sim_tick_index(),
			origin[0], origin[1], origin[2],
			origin_shifts_observed,
			int(b_stats.get("tier0", 0)),
			int(b_stats.get("tier1", 0)),
			int(b_stats.get("tier2", 0)),
			int(s_stats.get("resident_cells", 0))
		] +
		"[WASD] Move/Pan | [Tab] Toggle FP/RTS | [F1] Command Deck [F2] Scrubber Bay [F3] Rover Garage [F4] 50x Fast-Forward [F5] Rewind"
	)


func _run_automated_showcase_verification() -> void:
	print("=== OUTPOST MERIDIAN AUTOMATED VERIFICATION ===")
	var cmd_term := find_child("CommandDeckTerminal", true, false) as TerminalDisplay3D
	var scrub_term := find_child("ScrubberBayTerminal", true, false) as TerminalDisplay3D
	var fp_cam := fp_rig.get_node("Camera3D") as Camera3D
	var rts_cam := rts_rig.get_node("Camera3D") as Camera3D

	for f in 12:
		await get_tree().process_frame

	var b_stats: Dictionary = HydrationBubble.get_stats()
	var s_stats: Dictionary = StreamingServer.get_stats()
	var total_agents: int = SimulationServer.get_agent_count()
	print("1. Fleet spawned: ", total_agents, " | Tier 1 proxies: ", b_stats.get("tier1", 0), " | Tier 2 hydrated: ", b_stats.get("tier2", 0), " | Streamed Macro cells: ", s_stats.get("resident_cells", 0))

	# Step 1: Link orbital telemetry on Command Deck Terminal
	var link_res: Dictionary = cmd_term.execute_command("colony.link_telemetry")
	print("2. Command Deck colony.link_telemetry ok=", link_res.get("ok"))
	for f in 4:
		await get_tree().process_frame

	# Step 2: Switch to Orbital RTS Perspective via Terminal Command ('view.mode rts')
	var view_res: Dictionary = cmd_term.execute_command("view.mode rts")
	var rts_active_ok: bool = is_rts_mode and rts_cam.current
	print("3. Perspective swap to Orbital RTS ok=", rts_active_ok, " -> ", view_res.get("output"))

	# Step 3: Boost O2 Scrubbers & Overclock Hauler Fleet, then engage 50x Multi-Clock Fast-Forward
	SimulationServer.capture_rewind_snapshot_now()
	var saved_tick: int = SandboxClock.get_sim_tick_index()
	var scrub_res: Dictionary = scrub_term.execute_command("colony.scrubber_boost true")
	var fleet_res: Dictionary = cmd_term.execute_command("fleet.overclock true")
	var speed_res: Dictionary = cmd_term.execute_command("sim.speed 50")
	print("4. 50x Multi-Clock engaged: sim_time_scale=", SandboxClock.get_sim_time_scale(), " | scrub_ok=", scrub_res.get("ok"), " | fleet_ok=", fleet_res.get("ok"))

	# Advance 80 simulation ticks (representing rapid 50x colony production)
	SandboxClock.step_immediate(80)
	for f in 12:
		await get_tree().process_frame

	var mission_completed: bool = MissionRunner.get_mission_status(&"meridian_expansion") == MissionRunner.STATUS_COMPLETED
	var o2_after: float = WorldState.get_float(&"colony.o2_pressure_kpa")
	var ore_after: float = WorldState.get_float(&"colony.refined_ore_tons")
	var fp_zoom_applied: bool = fp_cam.fov < 70.0
	var rts_zoom_applied: bool = rts_cam.size < 35.0
	print("5. Colony Quota Reached: mission_completed=", mission_completed, " | O2=", o2_after, " kPa | Ore=", ore_after, " t | Dual-Domain Cutscene Zoom (FP FOV=", fp_cam.fov, ", RTS OrthoSize=", rts_cam.size, ")")

	# Step 4: Test Deterministic Rewind from Predictive Telemetry Console
	var ticks_back: int = max(1, int(SandboxClock.get_sim_tick_index() - saved_tick))
	var rw_err: Error = SimulationServer.rewind(ticks_back)
	var o2_rewound: float = WorldState.get_float(&"colony.o2_pressure_kpa")
	print("6. Predictive Telemetry Rewind (", ticks_back, " ticks): err=", rw_err, " | Rewound O2=", o2_rewound, " kPa")

	# Step 5: Switch back to 1st-Person Rover & Traverse > 4,500m across 2 OriginShifter 2048m thresholds
	cmd_term.execute_command("view.mode fp")
	for step_idx in 2:
		fp_rig.global_position += Vector3(2300.0, 0.0, 0.0)
		for f in 6:
			await get_tree().physics_frame
			await get_tree().process_frame

	var final_origin: PackedFloat64Array = OriginShifter.get_origin_64()
	print("7. High-Speed Crater Traversal: origin_shifts_observed=", origin_shifts_observed, " | Authoritative Origin64=[", final_origin[0], ", ", final_origin[1], ", ", final_origin[2], "] | Local Rover X=", fp_rig.global_position.x)

	var all_passed: bool = (
		total_agents == 300 and
		int(b_stats.get("tier2", 0)) > 0 and
		int(b_stats.get("tier1", 0)) > 0 and
		int(s_stats.get("resident_cells", 0)) > 0 and
		bool(link_res.get("ok", false)) and
		rts_active_ok and
		is_equal_approx(SandboxClock.get_sim_time_scale(), 50.0) and
		mission_completed and
		fp_zoom_applied and
		rts_zoom_applied and
		rw_err == OK and
		o2_rewound < 80.0 and
		origin_shifts_observed >= 2 and
		final_origin[0] >= 4096.0
	)
	print("=== OUTPOST MERIDIAN VERIFICATION: ", "PASS" if all_passed else "FAIL", " ===")
	get_tree().quit(0 if all_passed else 1)
