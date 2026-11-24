class_name Sector7Manager
extends Node3D

## Orchestrates the Gridlock: Sector 7 showcase scene.
## Initializes SimulationServer modules, StreamingServer manifest, HydrationBubble pools,
## domain CommandBus commands, live lighting feedback, and the HUD overlay.

@export var power_module: Resource
@export var traffic_module: Resource
@export var blackout_mission: Resource
@export var partition_manifest: Resource
@export var cruiser_prefab: PackedScene
@export var drone_prefab: PackedScene
@export var cruiser_proxy_mesh: Mesh
@export var drone_proxy_mesh: Mesh

var power_module_rid: RID
var traffic_module_rid: RID
var hud_label: RichTextLabel
var district_lights: Array[OmniLight3D] = []
var substation_beacon: OmniLight3D


func _ready() -> void:
	OriginShifter.register_root(self)

	# 1. Register Custom Domain Commands on CommandBus
	_register_domain_commands()

	# 2. Start SimulationServer Modules & Rewind Ring Buffer
	SimulationServer.set_rewind_capture_enabled(true)
	SimulationServer.set_snapshot_interval_ticks(10)
	if power_module is SimModule:
		power_module_rid = SimulationServer.register_module(power_module)
	if traffic_module is SimModule:
		traffic_module_rid = SimulationServer.register_module(traffic_module)

	# 3. Enable World Partition Streaming if manifest is present
	if partition_manifest is WorldPartitionManifest:
		if StreamingServer.set_manifest(partition_manifest) == OK:
			StreamingServer.set_enabled(true)

	# 4. Configure HydrationBubble Pools & Spawn 240 District Agents (Tier 0/1/2)
	_setup_hydration_bubble()

	# 5. Register & Start the Branching Sector 7 Mission
	if blackout_mission is MissionData:
		MissionRunner.register_mission(blackout_mission)
		MissionRunner.start_mission(blackout_mission.mission_id)

	# 6. Collect dynamic lights & connect EventBus listeners
	for child in find_children("*", "OmniLight3D", true, false):
		if child.name == "SubstationBeacon":
			substation_beacon = child as OmniLight3D
		elif child.is_in_group(&"street_light"):
			district_lights.append(child as OmniLight3D)

	hud_label = get_node_or_null("HUD/Panel/Margin/TelemetryLabel") as RichTextLabel
	EventBus.subscribe(&"grid.stabilized", _on_grid_stabilized)
	EventBus.subscribe(&"world_state_restored", _on_world_state_restored)

	# Run automated verification if launched with --verify-gridlock
	if "--verify-gridlock" in OS.get_cmdline_user_args():
		_run_automated_showcase_verification.call_deferred()


func _exit_tree() -> void:
	EventBus.unsubscribe(&"grid.stabilized", _on_grid_stabilized)
	EventBus.unsubscribe(&"world_state_restored", _on_world_state_restored)
	HydrationBubble.stop()
	StreamingServer.set_enabled(false)
	if power_module_rid.is_valid():
		SimulationServer.unregister_module(power_module_rid)
	if traffic_module_rid.is_valid():
		SimulationServer.unregister_module(traffic_module_rid)
	OriginShifter.unregister_root(self)
	_unregister_domain_commands()


func _process(_delta: float) -> void:
	var dimmer: float = WorldState.get_float(&"power.streetlights_dimmer", 0.35)
	for light in district_lights:
		if is_instance_valid(light):
			light.light_energy = 2.2 * dimmer
	if is_instance_valid(substation_beacon):
		var stabilized: bool = WorldState.get_bool(&"power.grid_stabilized", false)
		substation_beacon.light_color = Color(0.15, 1.0, 0.45) if stabilized else Color(1.0, 0.22, 0.15)
	_update_hud()


func _register_domain_commands() -> void:
	CommandBus.register_command(
		&"power.status",
		CommandBus.PERMISSION_GUEST,
		[],
		_cmd_power_status,
		"Displays Sector 7 substation load, core temperature, and valve states."
	)
	CommandBus.register_command(
		&"power.diagnose",
		CommandBus.PERMISSION_USER,
		[],
		_cmd_power_diagnose,
		"Runs a field diagnostic scan on the Sector 7 transformer bus."
	)
	CommandBus.register_command(
		&"power.coolant",
		CommandBus.PERMISSION_USER,
		[TYPE_BOOL],
		_cmd_power_coolant,
		"Opens or closes the auxiliary cryo-coolant valve (power.coolant true|false)."
	)
	CommandBus.register_command(
		&"power.vent_corp",
		CommandBus.PERMISSION_ADMIN,
		[],
		_cmd_power_vent_corp,
		"[ADMIN] Shunts excess Sector 7 overload current into the Corporate Plaza grid."
	)
	CommandBus.register_command(
		&"traffic.override",
		CommandBus.PERMISSION_USER,
		[TYPE_BOOL],
		_cmd_traffic_override,
		"Synchronizes Sector 7 traffic lights to green (traffic.override true|false)."
	)


func _unregister_domain_commands() -> void:
	for cmd in [&"power.status", &"power.diagnose", &"power.coolant", &"power.vent_corp", &"traffic.override"]:
		if CommandBus.has_command(cmd):
			CommandBus.unregister_command(cmd)


func _cmd_power_status() -> String:
	var load_mw: float = WorldState.get_float(&"power.sector7_load_mw", 94.0)
	var temp_c: float = WorldState.get_float(&"power.transformer_temp_c", 112.0)
	var coolant: bool = WorldState.get_bool(&"power.coolant_valve_open", false)
	var vented: bool = WorldState.get_bool(&"power.corp_sector_vented", false)
	var stable: bool = WorldState.get_bool(&"power.grid_stabilized", false)
	return "SECTOR 7 SUBSTATION | Load: %.1f MW | Temp: %.1f C | Coolant: %s | CorpVent: %s | Status: %s" % [
		load_mw, temp_c, "OPEN" if coolant else "CLOSED", "ACTIVE" if vented else "OFF", "STABILIZED" if stable else "OVERLOAD"
	]


func _cmd_power_diagnose() -> String:
	WorldState.set_bool(&"mission.kiosk_diagnosed", true)
	EventBus.emit(&"grid.kiosk_diagnostic_run", {"sector": "sector_7"})
	return "DIAGNOSTIC COMPLETE: Transformer #4 thermal runaway detected. Recommend opening Coolant Valve [F3] or shunting load at Substation Admin Console [F2]."


func _cmd_power_coolant(open: bool) -> String:
	WorldState.set_bool(&"power.coolant_valve_open", open)
	if open:
		EventBus.emit(&"grid.coolant_opened", {"manual": true})
	return "AUXILIARY CRYO-COOLANT VALVE: %s" % ("OPENED — Thermal load dropping." if open else "CLOSED")


func _cmd_power_vent_corp() -> String:
	WorldState.set_bool(&"power.corp_sector_vented", true)
	EventBus.emit(&"grid.corp_surge_vented", {"target": "corp_plaza"})
	return "ADMIN OVERRIDE EXECUTED: 42 MW shunted to Corporate Plaza. Sector 7 load normalizing."


func _cmd_traffic_override(green: bool) -> String:
	WorldState.set_bool(&"traffic.signals_green", green)
	if green:
		EventBus.emit(&"traffic.corridor_cleared", {})
	return "TRAFFIC CORRIDOR SIGNALS: %s" % ("SYNCHRONIZED GREEN (20 m/s)" if green else "DEFAULT CYCLE (6 m/s)")


func _setup_hydration_bubble() -> void:
	if cruiser_prefab and cruiser_proxy_mesh:
		HydrationBubble.configure_archetype(&"cyber_cruiser", cruiser_prefab, 16, cruiser_proxy_mesh, HydrationBubble.RECYCLE_FARTHEST_UNPINNED)
		HydrationBubble.set_radii(&"cyber_cruiser", 95.0, 120.0, 380.0)
	if drone_prefab and drone_proxy_mesh:
		HydrationBubble.configure_archetype(&"street_drone", drone_prefab, 24, drone_proxy_mesh, HydrationBubble.KEEP_TIER1_PROXY)
		HydrationBubble.set_radii(&"street_drone", 65.0, 85.0, 260.0)
	HydrationBubble.start(self)

	# Spawn 80 cyber cruisers and 120 street drones along the 800m boulevard
	if SimulationServer.get_agent_count() == 0:
		for i in 80:
			var lane_x: float = 5.5 if (i % 2 == 0) else -5.5
			var z: float = -360.0 + float(i) * 9.0
			var speed: float = 12.0 if (i % 2 == 0) else -12.0
			var rot_y: float = 0.0 if (i % 2 == 0) else PI
			SimulationServer.create_agent(&"cyber_cruiser", lane_x, 0.6, z, Vector3(0.0, 0.0, speed), rot_y, 100.0, 0)
		for i in 120:
			var sidewalk_x: float = 14.5 if (i % 2 == 0) else -14.5
			var z: float = -360.0 + float(i) * 6.0
			var speed: float = 1.8 if (i % 2 == 0) else -1.8
			var rot_y: float = 0.0 if (i % 2 == 0) else PI
			SimulationServer.create_agent(&"street_drone", sidewalk_x, 1.1, z, Vector3(0.0, 0.0, speed), rot_y, 100.0, 0)


func _on_grid_stabilized(_payload: Dictionary) -> void:
	var player := get_node_or_null("FixerPlayer")
	if player and player.has_node("AnimationPlayer"):
		var anim := player.get_node("AnimationPlayer") as AnimationPlayer
		if anim.has_animation(&"grid_restored_cutscene"):
			anim.play(&"grid_restored_cutscene")


func _on_world_state_restored(_payload: Dictionary) -> void:
	_update_hud()


func _update_hud() -> void:
	if not is_instance_valid(hud_label):
		return
	var load_mw: float = WorldState.get_float(&"power.sector7_load_mw", 94.0)
	var temp_c: float = WorldState.get_float(&"power.transformer_temp_c", 112.0)
	var stable: bool = WorldState.get_bool(&"power.grid_stabilized", false)
	var b_stats: Dictionary = HydrationBubble.get_stats()
	var s_stats: Dictionary = StreamingServer.get_stats()
	var active_objs: PackedStringArray = MissionRunner.get_active_objectives(&"sector7_blackout")
	var m_status: int = MissionRunner.get_mission_status(&"sector7_blackout")
	var status_str: String = "COMPLETED" if m_status == MissionRunner.STATUS_COMPLETED else ("ACTIVE: " + ", ".join(active_objs))

	hud_label.text = (
		"[b]GRIDLOCK: SECTOR 7 — SANDBOX TELEMETRY[/b]\n" +
		"Mission [sector7_blackout]: %s\n" % status_str +
		"Substation: %.1f MW | Core Temp: %.1f °C | Grid: %s\n" % [load_mw, temp_c, "NOMINAL" if stable else "CRITICAL OVERLOAD"] +
		"SimClock: %.1fx (Tick #%d) | Hydration T0/T1/T2: %d / %d / %d | Streamed Cells: %d\n" % [
			SandboxClock.get_sim_time_scale(),
			SandboxClock.get_sim_tick_index(),
			int(b_stats.get("tier0", 0)),
			int(b_stats.get("tier1", 0)),
			int(b_stats.get("tier2", 0)),
			int(s_stats.get("resident_cells", 0))
		] +
		"[WASD+Shift] Move | [Right-Mouse] Look | [Left-Click Terminal] Engage CLI (Esc to Exit) | [F1] Kiosk [F2] Substation [F3] Valve [F4] 10x Sim [F5] Rewind"
	)


func _run_automated_showcase_verification() -> void:
	print("=== GRIDLOCK: SECTOR 7 AUTOMATED VERIFICATION ===")
	var kiosk := find_child("StreetKioskTerminal", true, false) as TerminalDisplay3D
	var admin_term := find_child("SubstationAdminTerminal", true, false) as TerminalDisplay3D
	var valve_term := find_child("CoolantValveTerminal", true, false) as TerminalDisplay3D
	var player := get_node_or_null("FixerPlayer") as Node3D

	for f in 12:
		await get_tree().process_frame

	var b_stats: Dictionary = HydrationBubble.get_stats()
	var s_stats: Dictionary = StreamingServer.get_stats()
	var total_agents: int = SimulationServer.get_agent_count()
	var tier2_initial: int = int(b_stats.get("tier2", 0))
	var tier1_initial: int = int(b_stats.get("tier1", 0))
	print("1. Agents spawned: ", total_agents, " | Tier 1 proxies: ", tier1_initial, " | Tier 2 hydrated: ", tier2_initial, " | Resident streamed cells: ", s_stats.get("resident_cells", 0))

	# Step 1: Run diagnostic on Street Kiosk (USER permission, power & traffic namespaces)
	var diag_res: Dictionary = kiosk.execute_command("power.diagnose")
	var unauth_res: Dictionary = kiosk.execute_command("power.vent_corp")
	print("2. Street Kiosk power.diagnose ok=", diag_res.get("ok"), " | power.vent_corp blocked on USER kiosk ok=", not unauth_res.get("ok"))

	for f in 4:
		await get_tree().process_frame

	var active_after_diag: PackedStringArray = MissionRunner.get_active_objectives(&"sector7_blackout")
	print("3. Active branching objectives after diagnosis: ", active_after_diag)

	# Step 2: Capture snapshot, then execute Admin corporate surge vent on Substation Admin Terminal
	SimulationServer.capture_rewind_snapshot_now()
	var pre_vent_tick: int = SandboxClock.get_sim_tick_index()
	var vent_res: Dictionary = admin_term.execute_command("power.vent_corp")
	print("4. Substation Admin power.vent_corp ok=", vent_res.get("ok"), " -> ", vent_res.get("output"))

	# Step 3: Step simulation clock 60 ticks so PowerGridSim cools down & stabilizes
	SandboxClock.step_immediate(60)
	for f in 12:
		await get_tree().process_frame

	var mission_done_branch_a: bool = MissionRunner.get_mission_status(&"sector7_blackout") == MissionRunner.STATUS_COMPLETED
	var fov_cutscene: float = (player.get_node("Camera3D") as Camera3D).fov
	print("5. Branch A (Corporate Vent) completed mission=", mission_done_branch_a, " | Load MW=", WorldState.get_float(&"power.sector7_load_mw"), " | Temp C=", WorldState.get_float(&"power.transformer_temp_c"), " | Cutscene FOV=", fov_cutscene)

	# Step 4: Test deterministic rewind + alternative Branch B (Coolant Valve repair)
	var ticks_to_rewind: int = max(1, int(SandboxClock.get_sim_tick_index() - pre_vent_tick))
	var rw_err: Error = SimulationServer.rewind(ticks_to_rewind)
	var valve_res: Dictionary = valve_term.execute_command("power.coolant true")
	SandboxClock.step_immediate(60)
	for f in 12:
		await get_tree().process_frame

	var coolant_open: bool = WorldState.get_bool(&"power.coolant_valve_open")
	var mission_done_branch_b: bool = MissionRunner.get_mission_status(&"sector7_blackout") == MissionRunner.STATUS_COMPLETED
	print("6. Rewind err=", rw_err, " | Branch B (Coolant Valve) open=", coolant_open, " | Final Temp C=", WorldState.get_float(&"power.transformer_temp_c"), " | Branch B completed=", mission_done_branch_b)

	var all_passed: bool = (
		total_agents == 200 and
		tier2_initial > 0 and
		tier1_initial > 0 and
		int(s_stats.get("resident_cells", 0)) > 0 and
		bool(diag_res.get("ok", false)) and
		not bool(unauth_res.get("ok", true)) and
		active_after_diag.size() >= 2 and
		bool(vent_res.get("ok", false)) and
		mission_done_branch_a and
		rw_err == OK and
		coolant_open and
		mission_done_branch_b
	)
	print("=== GRIDLOCK VERIFICATION: ", "PASS" if all_passed else "FAIL", " ===")
	get_tree().quit(0 if all_passed else 1)
