extends SceneTree

## Builder script that generates all native .tres resources, prefabs, HLOD baked cells,
## custom 3-track animations, and the playable 3D cyberpunk district scene for Gridlock: Sector 7.

const PowerGridSimScript = preload("res://scripts/power_grid_sim.gd")
const TrafficCrowdSimScript = preload("res://scripts/traffic_crowd_sim.gd")
const HydratedVehicleScript = preload("res://scripts/hydrated_vehicle_controller.gd")
const FixerPlayerScript = preload("res://scripts/fixer_player_controller.gd")
const Sector7ManagerScript = preload("res://scripts/sector7_manager.gd")


func _initialize() -> void:
	_build_all.call_deferred()


func _node_key(id: String) -> String:
	return "node_" + id.to_utf8_buffer().hex_encode()


func _build_all() -> void:
	DirAccess.make_dir_recursive_absolute("res://resources")
	DirAccess.make_dir_recursive_absolute("res://scenes")

	# 1. Create & Save SandboxSchemaRegistry
	var schema := SandboxSchemaRegistry.new()
	schema.register_state_key(&"power.sector7_load_mw", TYPE_FLOAT, 94.0, Vector2(0.0, 150.0), false, "Current load in Megawatts on Sector 7 primary substation.")
	schema.register_state_key(&"power.transformer_temp_c", TYPE_FLOAT, 112.0, Vector2(20.0, 250.0), false, "Core transformer temperature in degrees Celsius.")
	schema.register_state_key(&"power.coolant_valve_open", TYPE_BOOL, false, Vector2.ZERO, false, "True when auxiliary cryo-coolant valve is open.")
	schema.register_state_key(&"power.corp_sector_vented", TYPE_BOOL, false, Vector2.ZERO, false, "True when overload current is shunted into the corporate plaza.")
	schema.register_state_key(&"power.grid_stabilized", TYPE_BOOL, false, Vector2.ZERO, false, "True once Sector 7 load drops below 72 MW and temp below 82 C.")
	schema.register_state_key(&"power.streetlights_dimmer", TYPE_FLOAT, 0.35, Vector2(0.0, 1.5), false, "District streetlight and neon brightness multiplier.")
	schema.register_state_key(&"traffic.signals_green", TYPE_BOOL, false, Vector2.ZERO, false, "True when Sector 7 traffic corridor signals are synchronized green.")
	schema.register_state_key(&"traffic.avg_speed_mps", TYPE_FLOAT, 12.0, Vector2(0.0, 40.0), false, "Target traffic speed along the Sector 7 artery in m/s.")
	schema.register_state_key(&"traffic.active_agents", TYPE_INT, 200, Vector2(0.0, 2000.0), false, "Total simulated vehicles and pedestrians in Sector 7.")
	schema.register_state_key(&"mission.kiosk_diagnosed", TYPE_BOOL, false, Vector2.ZERO, false, "True once the field technician runs power.diagnose at the kiosk.")
	schema.register_state_key(&"mission.resolution_branch", TYPE_STRING, "pending", Vector2.ZERO, false, "Records which resolution path stabilized Sector 7.")

	schema.register_event(&"grid.kiosk_diagnostic_run", {"sector": TYPE_STRING}, false, "Emitted when power.diagnose is executed.")
	schema.register_event(&"grid.coolant_opened", {"manual": TYPE_BOOL}, false, "Emitted when auxiliary cryo-coolant valve opens.")
	schema.register_event(&"grid.corp_surge_vented", {"target": TYPE_STRING}, false, "Emitted when overload is shunted to Corporate Plaza.")
	schema.register_event(&"grid.overload_alarm", {"temp_c": TYPE_FLOAT, "load_mw": TYPE_FLOAT}, false, "Emitted on transformer thermal warning.")
	schema.register_event(&"grid.stabilized", {"route": TYPE_STRING, "load_mw": TYPE_FLOAT}, false, "Emitted when Sector 7 grid stabilizes.")
	schema.register_event(&"traffic.corridor_cleared", {}, false, "Emitted when traffic signals are overridden to green.")

	schema.register_actor_id(&"fixer_player", "Field Technician player rig.")
	schema.register_actor_id(&"substation_core", "Sector 7 primary transformer substation.")
	schema.register_marker_id(&"MarkerKiosk", "Street corner diagnostic kiosk waypoint.")
	schema.register_marker_id(&"MarkerSubstation", "Substation Admin Console waypoint.")
	schema.register_marker_id(&"MarkerValve", "Auxiliary Coolant Valve terminal waypoint.")
	schema.register_archetype_id(&"cyber_cruiser", "Hydratable autonomous street cruiser.")
	schema.register_archetype_id(&"street_drone", "Hydratable sidewalk courier drone.")
	schema.register_tag(&"player", "Player controlled actor.")
	schema.register_tag(&"vehicle", "Traffic vehicle actor.")
	schema.register_tag(&"infrastructure", "District power grid node.")

	ResourceSaver.save(schema, "res://resources/sandbox_schema.tres")
	schema = ResourceLoader.load("res://resources/sandbox_schema.tres")

	# 2. Create & Save SimModule Resources
	var power_mod := SimModule.new()
	power_mod.set_script(PowerGridSimScript)
	power_mod.module_id = &"power_grid_sim"
	power_mod.tick_rate_hz = 10.0
	ResourceSaver.save(power_mod, "res://resources/power_grid_sim.tres")
	power_mod = ResourceLoader.load("res://resources/power_grid_sim.tres")

	var traffic_mod := SimModule.new()
	traffic_mod.set_script(TrafficCrowdSimScript)
	traffic_mod.module_id = &"traffic_crowd_sim"
	traffic_mod.tick_rate_hz = 5.0
	ResourceSaver.save(traffic_mod, "res://resources/traffic_crowd_sim.tres")
	traffic_mod = ResourceLoader.load("res://resources/traffic_crowd_sim.tres")

	# 3. Create & Save Start and Complete SequenceData Resources (with GraphEdit layout metadata)
	var seq_start := SequenceData.new()
	seq_start.sequence_id = &"seq_blackout_alert"
	seq_start.entry_step_uuid = &"step_dim_lights"

	var step1 := ActionSetState.new()
	step1.step_uuid = &"step_dim_lights"
	step1.key = &"power.streetlights_dimmer"
	step1.value = 0.35
	step1.next_steps = PackedStringArray(["step_emit_alarm"])

	var step2 := ActionEmitEvent.new()
	step2.step_uuid = &"step_emit_alarm"
	step2.event_id = &"grid.overload_alarm"
	step2.payload = {"sector": "sector_7", "temp_c": 112.0}

	var start_steps: Array[SequenceAction] = [step1, step2]
	seq_start.steps = start_steps
	seq_start.set_editor_graph_meta({
		"entry": {"position": Vector2(40, 120)},
		_node_key("step_dim_lights"): {"position": Vector2(340, 100)},
		_node_key("step_emit_alarm"): {"position": Vector2(660, 100)}
	})
	ResourceSaver.save(seq_start, "res://resources/seq_blackout_alert.tres")
	seq_start = ResourceLoader.load("res://resources/seq_blackout_alert.tres")

	var seq_complete := SequenceData.new()
	seq_complete.sequence_id = &"seq_grid_restored"
	seq_complete.entry_step_uuid = &"step_branch_check"

	var cond_was_vented := CondStateEquals.new()
	cond_was_vented.key = &"power.corp_sector_vented"
	cond_was_vented.expected = true

	var step_branch := ActionBranchCondition.new()
	step_branch.step_uuid = &"step_branch_check"
	step_branch.condition = cond_was_vented
	step_branch.true_steps = PackedStringArray(["step_mark_corp_vent"])
	step_branch.false_steps = PackedStringArray(["step_mark_coolant"])

	var step_corp := ActionSetState.new()
	step_corp.step_uuid = &"step_mark_corp_vent"
	step_corp.key = &"mission.resolution_branch"
	step_corp.value = "corp_surge_vented"
	step_corp.next_steps = PackedStringArray(["step_restore_lights"])

	var step_cool := ActionSetState.new()
	step_cool.step_uuid = &"step_mark_coolant"
	step_cool.key = &"mission.resolution_branch"
	step_cool.value = "cryo_coolant_repaired"
	step_cool.next_steps = PackedStringArray(["step_restore_lights"])

	var step_lights := ActionSetState.new()
	step_lights.step_uuid = &"step_restore_lights"
	step_lights.key = &"power.streetlights_dimmer"
	step_lights.value = 1.25
	step_lights.next_steps = PackedStringArray(["step_green_wave"])

	var step_green := ActionRunCommand.new()
	step_green.step_uuid = &"step_green_wave"
	step_green.command_string = "traffic.override true"

	var complete_steps: Array[SequenceAction] = [step_branch, step_corp, step_cool, step_lights, step_green]
	seq_complete.steps = complete_steps
	seq_complete.set_editor_graph_meta({
		"entry": {"position": Vector2(40, 180)},
		_node_key("step_branch_check"): {"position": Vector2(340, 160)},
		_node_key("step_mark_corp_vent"): {"position": Vector2(680, 60)},
		_node_key("step_mark_coolant"): {"position": Vector2(680, 280)},
		_node_key("step_restore_lights"): {"position": Vector2(1020, 160)},
		_node_key("step_green_wave"): {"position": Vector2(1340, 160)}
	})
	ResourceSaver.save(seq_complete, "res://resources/seq_grid_restored.tres")
	seq_complete = ResourceLoader.load("res://resources/seq_grid_restored.tres")

	# 4. Create & Save Branching MissionData Resource (with GraphFrame comments & layout metadata)
	var mission := MissionData.new()
	mission.mission_id = &"sector7_blackout"
	mission.entry_objective_id = &"obj_diagnose_kiosk"
	mission.use_first_as_entry = false
	mission.on_start_sequence = seq_start
	mission.on_complete_sequence = seq_complete

	# Objective 1: Run diagnostic at Street Kiosk
	var obj1 := ObjectiveData.new()
	obj1.objective_id = &"obj_diagnose_kiosk"
	obj1.title = "1. Run Diagnostic at Street Kiosk [F1]"
	obj1.description = "Engage the Street Corner Kiosk and run 'power.diagnose'."
	obj1.required = true
	var cond_diag := CondStateEquals.new()
	cond_diag.key = &"mission.kiosk_diagnosed"
	cond_diag.expected = true
	obj1.complete_condition = cond_diag

	# Objective 2A (Optional Side Branch A): Open Cryo-Coolant Valve
	var obj2a := ObjectiveData.new()
	obj2a.objective_id = &"obj_open_coolant_valve"
	obj2a.title = "2A. [Repair Route] Open Cryo-Coolant Valve [F3]"
	obj2a.description = "Engage the Coolant Valve terminal and run 'power.coolant true'."
	obj2a.required = false
	var cond_coolant := CondStateEquals.new()
	cond_coolant.key = &"power.coolant_valve_open"
	cond_coolant.expected = true
	var cond_grid_stable := CondStateEquals.new()
	cond_grid_stable.key = &"power.grid_stabilized"
	cond_grid_stable.expected = true
	var cond_2a_done := CondAny.new()
	var c2a_arr: Array[SandboxCondition] = [cond_coolant, cond_grid_stable]
	cond_2a_done.children = c2a_arr
	obj2a.complete_condition = cond_2a_done

	# Objective 2B (Optional Side Branch B): Shunt Overload to Corporate Plaza
	var obj2b := ObjectiveData.new()
	obj2b.objective_id = &"obj_vent_corp_grid"
	obj2b.title = "2B. [Hack Route] Shunt Load at Admin Console [F2]"
	obj2b.description = "Engage the Substation Admin Console and run 'power.vent_corp'."
	obj2b.required = false
	var cond_vent := CondStateEquals.new()
	cond_vent.key = &"power.corp_sector_vented"
	cond_vent.expected = true
	var cond_2b_done := CondAny.new()
	var c2b_arr: Array[SandboxCondition] = [cond_vent, cond_grid_stable]
	cond_2b_done.children = c2b_arr
	obj2b.complete_condition = cond_2b_done

	# Objective 3 (Converging Final Objective): Verify Substation Stabilization
	var obj3 := ObjectiveData.new()
	obj3.objective_id = &"obj_verify_grid_stable"
	obj3.title = "3. Verify Sector 7 Grid Stabilization (< 72 MW & < 82 C)"
	obj3.description = "Wait for thermal runaway to subside after executing either branch."
	obj3.required = true

	var cond_load_ok := CondStateRange.new()
	cond_load_ok.key = &"power.sector7_load_mw"
	cond_load_ok.min_val = 0.0
	cond_load_ok.max_val = 72.0

	var cond_temp_ok := CondStateRange.new()
	cond_temp_ok.key = &"power.transformer_temp_c"
	cond_temp_ok.min_val = 20.0
	cond_temp_ok.max_val = 82.0

	var cond_either_branch := CondAny.new()
	var branch_children: Array[SandboxCondition] = [cond_coolant, cond_vent]
	cond_either_branch.children = branch_children

	var cond_all_stable := CondAll.new()
	var all_children: Array[SandboxCondition] = [cond_load_ok, cond_temp_ok, cond_either_branch]
	cond_all_stable.children = all_children
	obj3.complete_condition = cond_all_stable

	var objs: Array[ObjectiveData] = [obj1, obj2a, obj2b, obj3]
	mission.objectives = objs

	var tr1 := MissionTransition.new()
	tr1.from_objective_id = &"obj_diagnose_kiosk"
	tr1.to_objective_id = &"obj_open_coolant_valve"

	var tr2 := MissionTransition.new()
	tr2.from_objective_id = &"obj_diagnose_kiosk"
	tr2.to_objective_id = &"obj_vent_corp_grid"

	var tr3 := MissionTransition.new()
	tr3.from_objective_id = &"obj_open_coolant_valve"
	tr3.to_objective_id = &"obj_verify_grid_stable"

	var tr4 := MissionTransition.new()
	tr4.from_objective_id = &"obj_vent_corp_grid"
	tr4.to_objective_id = &"obj_verify_grid_stable"

	var trs: Array[MissionTransition] = [tr1, tr2, tr3, tr4]
	mission.transitions = trs

	mission.set_editor_graph_meta({
		"entry": {"position": Vector2(40, 220)},
		"frame_phase1": {"rect": Rect2(Vector2(320, 40), Vector2(420, 380)), "title": "Phase 1: Field Diagnosis"},
		"frame_phase2": {"rect": Rect2(Vector2(780, 20), Vector2(460, 560)), "title": "Phase 2: Branching Resolution"},
		"frame_phase3": {"rect": Rect2(Vector2(1280, 100), Vector2(480, 420)), "title": "Phase 3: Grid Stabilization"},
		_node_key("obj_diagnose_kiosk"): {"position": Vector2(360, 120), "frame": &"frame_phase1"},
		_node_key("obj_open_coolant_valve"): {"position": Vector2(820, 80), "frame": &"frame_phase2"},
		_node_key("obj_vent_corp_grid"): {"position": Vector2(820, 340), "frame": &"frame_phase2"},
		_node_key("obj_verify_grid_stable"): {"position": Vector2(1320, 180), "frame": &"frame_phase3"}
	})
	ResourceSaver.save(mission, "res://resources/mission_sector7_blackout.tres")
	mission = ResourceLoader.load("res://resources/mission_sector7_blackout.tres")

	# 5. Create Hydratable Cruiser & Drone Prefabs + Proxy Meshes
	var cruiser_proxy := BoxMesh.new()
	cruiser_proxy.size = Vector3(2.1, 1.15, 4.4)
	var cruiser_mat := StandardMaterial3D.new()
	cruiser_mat.albedo_color = Color(0.12, 0.16, 0.24)
	cruiser_mat.emission_enabled = true
	cruiser_mat.emission = Color(0.1, 0.75, 1.0)
	cruiser_mat.emission_energy_multiplier = 1.4
	cruiser_proxy.material = cruiser_mat
	ResourceSaver.save(cruiser_proxy, "res://resources/cruiser_proxy_mesh.tres")
	cruiser_proxy = ResourceLoader.load("res://resources/cruiser_proxy_mesh.tres")

	var drone_proxy := CylinderMesh.new()
	drone_proxy.top_radius = 0.45
	drone_proxy.bottom_radius = 0.45
	drone_proxy.height = 1.6
	var drone_mat := StandardMaterial3D.new()
	drone_mat.albedo_color = Color(0.22, 0.2, 0.28)
	drone_mat.emission_enabled = true
	drone_mat.emission = Color(1.0, 0.55, 0.15)
	drone_mat.emission_energy_multiplier = 1.2
	drone_proxy.material = drone_mat
	ResourceSaver.save(drone_proxy, "res://resources/drone_proxy_mesh.tres")
	drone_proxy = ResourceLoader.load("res://resources/drone_proxy_mesh.tres")

	var cruiser_root := Node3D.new()
	cruiser_root.name = "CyberCruiser"
	cruiser_root.set_script(HydratedVehicleScript)
	var c_hyd := HydratableActor.new()
	c_hyd.name = "Hydratable"
	c_hyd.archetype_id = &"cyber_cruiser"
	c_hyd.hydrate_radius = 95.0
	c_hyd.dehydrate_radius = 120.0
	cruiser_root.add_child(c_hyd)
	c_hyd.owner = cruiser_root
	var c_mesh := MeshInstance3D.new()
	c_mesh.name = "BodyMesh"
	c_mesh.mesh = cruiser_proxy
	cruiser_root.add_child(c_mesh)
	c_mesh.owner = cruiser_root
	var c_light := OmniLight3D.new()
	c_light.name = "Headlight"
	c_light.position = Vector3(0.0, 0.4, 2.2)
	c_light.light_color = Color(0.2, 0.95, 1.0)
	c_light.light_energy = 1.5
	c_light.omni_range = 12.0
	cruiser_root.add_child(c_light)
	c_light.owner = cruiser_root
	var cruiser_scene := PackedScene.new()
	cruiser_scene.pack(cruiser_root)
	cruiser_root.free()
	ResourceSaver.save(cruiser_scene, "res://scenes/cyber_cruiser.tscn")
	cruiser_scene = ResourceLoader.load("res://scenes/cyber_cruiser.tscn")

	var drone_root := Node3D.new()
	drone_root.name = "StreetDrone"
	drone_root.set_script(HydratedVehicleScript)
	var d_hyd := HydratableActor.new()
	d_hyd.name = "Hydratable"
	d_hyd.archetype_id = &"street_drone"
	d_hyd.hydrate_radius = 65.0
	d_hyd.dehydrate_radius = 85.0
	drone_root.add_child(d_hyd)
	d_hyd.owner = drone_root
	var d_mesh := MeshInstance3D.new()
	d_mesh.name = "BodyMesh"
	d_mesh.mesh = drone_proxy
	drone_root.add_child(d_mesh)
	d_mesh.owner = drone_root
	var drone_scene := PackedScene.new()
	drone_scene.pack(drone_root)
	drone_root.free()
	ResourceSaver.save(drone_scene, "res://scenes/street_drone.tscn")
	drone_scene = ResourceLoader.load("res://scenes/street_drone.tscn")

	# 6. Build 4 Meso Partition Skyline Cells & WorldPartitionManifest
	DirAccess.make_dir_recursive_absolute("res://baked_cells/meso_1")

	var tower_mat_a := StandardMaterial3D.new()
	tower_mat_a.albedo_color = Color(0.11, 0.14, 0.22)
	tower_mat_a.roughness = 0.35
	tower_mat_a.metallic = 0.65

	var tower_mat_b := StandardMaterial3D.new()
	tower_mat_b.albedo_color = Color(0.18, 0.12, 0.25)
	tower_mat_b.roughness = 0.45
	tower_mat_b.metallic = 0.5

	var tower_mesh := BoxMesh.new()
	tower_mesh.size = Vector3(22.0, 48.0, 22.0)

	var manifest := WorldPartitionManifest.new()
	var meso_layer := WorldPartitionLayer.new()
	meso_layer.layer_id = &"meso"
	meso_layer.cell_size = 128.0
	meso_layer.load_radius = 256.0
	meso_layer.evict_radius = 384.0
	var layers_arr: Array[WorldPartitionLayer] = [meso_layer]
	manifest.layers = layers_arr

	var cell_coords := [Vector2i(-1, -1), Vector2i(0, -1), Vector2i(-1, 0), Vector2i(0, 0)]
	var cells_arr: Array[WorldPartitionCell] = []

	for c_idx in cell_coords.size():
		var coord: Vector2i = cell_coords[c_idx]
		var cell_id := "meso_%d_%d" % [coord.x, coord.y]
		var cell_root := Node3D.new()
		cell_root.name = "WorldPartitionCell"
		cell_root.add_to_group(&"sandbox_static")

		var st := SurfaceTool.new()
		st.begin(Mesh.PRIMITIVE_TRIANGLES)
		st.set_material(tower_mat_a if (c_idx % 2 == 0) else tower_mat_b)

		for t_i in 2:
			var tower := MeshInstance3D.new()
			tower.name = "SkylineTower_%d_%d" % [c_idx, t_i]
			tower.mesh = tower_mesh
			tower.material_override = tower_mat_a if (t_i % 2 == 0) else tower_mat_b
			var local_pos := Vector3(42.0 if coord.x == 0 else 86.0, 24.0, 35.0 + float(t_i) * 55.0)
			tower.position = local_pos
			cell_root.add_child(tower)
			tower.owner = cell_root
			st.append_from(tower_mesh, 0, Transform3D(Basis(), local_pos))

		var proxy_mesh: ArrayMesh = st.commit()
		var proxy_path := "res://baked_cells/meso_1/%s_proxy.res" % cell_id
		var high_path := "res://baked_cells/meso_1/%s.scn" % cell_id
		ResourceSaver.save(proxy_mesh, proxy_path, ResourceSaver.FLAG_COMPRESS)

		var cell_scene := PackedScene.new()
		cell_scene.pack(cell_root)
		cell_root.free()
		ResourceSaver.save(cell_scene, high_path, ResourceSaver.FLAG_COMPRESS)

		var wp_cell := WorldPartitionCell.new()
		wp_cell.cell_id = StringName(cell_id)
		wp_cell.layer = &"meso"
		wp_cell.grid_coords = coord
		wp_cell.hlod_impostor_path = proxy_path
		wp_cell.high_scene_path = high_path
		cells_arr.append(wp_cell)

	manifest.cells = cells_arr
	var bake_err := manifest.validate()
	var manifest_path := "res://baked_cells/meso_1/manifest.tres"
	ResourceSaver.save(manifest, manifest_path)
	manifest = ResourceLoader.load(manifest_path) as WorldPartitionManifest
	print("Partition Manifest generated: validate_err=", bake_err, " path=", manifest_path)

	# 7. Build Playable Main Scene (res://scenes/sector7_main.tscn)
	var main := Node3D.new()
	main.name = "Sector7District"
	main.set_script(Sector7ManagerScript)
	main.set("power_module", power_mod)
	main.set("traffic_module", traffic_mod)
	main.set("blackout_mission", mission)
	main.set("partition_manifest", manifest)
	main.set("cruiser_prefab", cruiser_scene)
	main.set("drone_prefab", drone_scene)
	main.set("cruiser_proxy_mesh", cruiser_proxy)
	main.set("drone_proxy_mesh", drone_proxy)

	# Environment & Directional Light
	var world_env := WorldEnvironment.new()
	world_env.name = "WorldEnvironment"
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.02, 0.03, 0.055)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.14, 0.18, 0.28)
	env.ambient_light_energy = 0.8
	env.glow_enabled = true
	env.glow_intensity = 0.65
	world_env.environment = env
	main.add_child(world_env)
	world_env.owner = main

	var sun := DirectionalLight3D.new()
	sun.name = "MoonLight"
	sun.rotation_degrees = Vector3(-42.0, -35.0, 0.0)
	sun.light_color = Color(0.55, 0.68, 0.95)
	sun.light_energy = 0.65
	sun.shadow_enabled = true
	main.add_child(sun)
	sun.owner = main

	# Roadway & Sidewalk Static Collider + Visual Boulevard
	var road_body := StaticBody3D.new()
	road_body.name = "BoulevardGround"
	main.add_child(road_body)
	road_body.owner = main

	var ground_col := CollisionShape3D.new()
	ground_col.name = "GroundCollider"
	var ground_box := BoxShape3D.new()
	ground_box.size = Vector3(240.0, 1.0, 840.0)
	ground_col.shape = ground_box
	ground_col.position = Vector3(0.0, -0.5, 0.0)
	road_body.add_child(ground_col)
	ground_col.owner = main

	var road_mesh_inst := MeshInstance3D.new()
	road_mesh_inst.name = "AsphaltMesh"
	var road_mesh := BoxMesh.new()
	road_mesh.size = Vector3(24.0, 0.1, 800.0)
	var road_mat := StandardMaterial3D.new()
	road_mat.albedo_color = Color(0.07, 0.08, 0.1)
	road_mat.roughness = 0.4
	road_mesh.material = road_mat
	road_mesh_inst.mesh = road_mesh
	road_body.add_child(road_mesh_inst)
	road_mesh_inst.owner = main

	# Substation Core Prop & Beacon Light
	var substation := Node3D.new()
	substation.name = "SubstationCore"
	substation.position = Vector3(0.0, 0.0, -28.0)
	main.add_child(substation)
	substation.owner = main

	var sub_actor := SandboxActor.new()
	sub_actor.name = "SandboxActor"
	sub_actor.actor_id = &"substation_core"
	sub_actor.tags = [&"infrastructure"]
	substation.add_child(sub_actor)
	sub_actor.owner = main

	var sub_mesh_inst := MeshInstance3D.new()
	sub_mesh_inst.name = "TransformerMesh"
	var sub_box := BoxMesh.new()
	sub_box.size = Vector3(6.5, 5.5, 6.5)
	var sub_mat := StandardMaterial3D.new()
	sub_mat.albedo_color = Color(0.16, 0.2, 0.26)
	sub_mat.metallic = 0.8
	sub_mat.roughness = 0.3
	sub_box.material = sub_mat
	sub_mesh_inst.mesh = sub_box
	sub_mesh_inst.position = Vector3(0.0, 2.75, 0.0)
	substation.add_child(sub_mesh_inst)
	sub_mesh_inst.owner = main

	var beacon := OmniLight3D.new()
	beacon.name = "SubstationBeacon"
	beacon.position = Vector3(0.0, 6.8, 0.0)
	beacon.light_color = Color(1.0, 0.22, 0.15)
	beacon.light_energy = 4.0
	beacon.omni_range = 28.0
	substation.add_child(beacon)
	beacon.owner = main

	# District Streetlights controlled by WorldState 'power.streetlights_dimmer'
	for i in 6:
		var lamp := OmniLight3D.new()
		lamp.name = "StreetLight_%d" % i
		lamp.add_to_group(&"street_light")
		lamp.position = Vector3(-11.0 if (i % 2 == 0) else 11.0, 6.0, -50.0 + float(i) * 20.0)
		lamp.light_color = Color(0.25, 0.85, 1.0)
		lamp.light_energy = 1.0
		lamp.omni_range = 24.0
		main.add_child(lamp)
		lamp.owner = main

	# 3D Scene Waypoint Markers (pickable with the 3D Eyedropper in SandboxIdPickerProperty)
	var marker_kiosk := Marker3D.new()
	marker_kiosk.name = "MarkerKiosk"
	marker_kiosk.position = Vector3(-9.0, 1.0, 8.0)
	main.add_child(marker_kiosk)
	marker_kiosk.owner = main

	var marker_sub := Marker3D.new()
	marker_sub.name = "MarkerSubstation"
	marker_sub.position = Vector3(0.0, 1.0, -18.0)
	main.add_child(marker_sub)
	marker_sub.owner = main

	var marker_valve := Marker3D.new()
	marker_valve.name = "MarkerValve"
	marker_valve.position = Vector3(9.5, 1.0, -10.0)
	main.add_child(marker_valve)
	marker_valve.owner = main

	# Player Rig (CharacterBody3D + Camera3D + SandboxActor + StreamingInvoker + 3-Track AnimationPlayer)
	var player := CharacterBody3D.new()
	player.name = "FixerPlayer"
	player.set_script(FixerPlayerScript)
	player.position = Vector3(-9.0, 1.1, 11.5)
	main.add_child(player)
	player.owner = main

	var p_col := CollisionShape3D.new()
	p_col.name = "Collider"
	var capsule := CapsuleShape3D.new()
	capsule.radius = 0.4
	capsule.height = 1.8
	p_col.shape = capsule
	player.add_child(p_col)
	p_col.owner = main

	var cam := Camera3D.new()
	cam.name = "Camera3D"
	cam.position = Vector3(0.0, 0.65, 0.0)
	cam.current = true
	player.add_child(cam)
	cam.owner = main

	var p_actor := SandboxActor.new()
	p_actor.name = "SandboxActor"
	p_actor.actor_id = &"fixer_player"
	p_actor.tags = [&"player"]
	player.add_child(p_actor)
	p_actor.owner = main

	var p_invoker := StreamingInvoker.new()
	p_invoker.name = "StreamingInvoker"
	p_invoker.lookahead = 3.0
	player.add_child(p_invoker)
	p_invoker.owner = main

	# Native 3-Track Custom Animation Cutscene (TYPE_WORLD_STATE, TYPE_EVENT_BUS, TYPE_ACTOR_COMMAND)
	var anim_player := AnimationPlayer.new()
	anim_player.name = "AnimationPlayer"
	var anim := Animation.new()
	anim.length = 2.5

	var tr_state := anim.add_track(Animation.TYPE_WORLD_STATE)
	anim.track_set_path(tr_state, NodePath("WorldState:power.streetlights_dimmer"))
	anim.track_insert_key(tr_state, 0.0, 0.4)
	anim.track_insert_key(tr_state, 1.5, 1.35)

	var tr_event := anim.add_track(Animation.TYPE_EVENT_BUS)
	anim.track_set_path(tr_event, NodePath("EventBus:traffic.corridor_cleared"))
	anim.event_bus_track_insert_key(tr_event, 0.5, &"traffic.corridor_cleared", {"cutscene": true})

	var tr_cmd := anim.add_track(Animation.TYPE_ACTOR_COMMAND)
	anim.track_set_path(tr_cmd, NodePath("ActorRegistry:fixer_player"))
	anim.actor_command_track_insert_key(tr_cmd, 0.1, &"fixer_player", &"camera.focus_substation", {"fov": 54.0, "marker_id": &"MarkerSubstation"})
	anim.actor_command_track_insert_key(tr_cmd, 2.2, &"fixer_player", &"camera.reset_fov", {"fov": 75.0})

	var lib := AnimationLibrary.new()
	lib.add_animation(&"grid_restored_cutscene", anim)
	anim_player.add_animation_library(&"", lib)
	player.add_child(anim_player)
	anim_player.owner = main

	# Three Diegetic 3D Terminals with Distinct Permission Tiers & Namespace Whitelists
	var kiosk_term := TerminalDisplay3D.new()
	kiosk_term.name = "StreetKioskTerminal"
	kiosk_term.title = "SECTOR 7 STREET KIOSK [USER: power.*, traffic.*]"
	kiosk_term.position = Vector3(-9.0, 1.65, 6.2)
	kiosk_term.screen_size = Vector2(2.1, 1.25)
	kiosk_term.permission_tier = CommandBus.PERMISSION_USER
	var kiosk_ns: Array[StringName] = [&"power", &"traffic", &"state", &"mission", &"help"]
	kiosk_term.allowed_namespaces = kiosk_ns
	kiosk_term.actor_path = NodePath("../FixerPlayer/SandboxActor")
	main.add_child(kiosk_term)
	kiosk_term.owner = main

	var admin_term := TerminalDisplay3D.new()
	admin_term.name = "SubstationAdminTerminal"
	admin_term.title = "SUBSTATION CORE CONSOLE [ADMIN: ALL NAMESPACES]"
	admin_term.position = Vector3(0.0, 1.65, -21.0)
	admin_term.screen_size = Vector2(2.4, 1.4)
	admin_term.permission_tier = CommandBus.PERMISSION_ADMIN
	admin_term.actor_path = NodePath("../FixerPlayer/SandboxActor")
	main.add_child(admin_term)
	admin_term.owner = main

	var valve_term := TerminalDisplay3D.new()
	valve_term.name = "CoolantValveTerminal"
	valve_term.title = "AUX CRYO-COOLANT VALVE [USER: power.coolant]"
	valve_term.position = Vector3(9.5, 1.65, -12.5)
	valve_term.screen_size = Vector2(2.0, 1.2)
	valve_term.permission_tier = CommandBus.PERMISSION_USER
	var valve_ns: Array[StringName] = [&"power", &"help"]
	valve_term.allowed_namespaces = valve_ns
	valve_term.actor_path = NodePath("../FixerPlayer/SandboxActor")
	main.add_child(valve_term)
	valve_term.owner = main

	# 2D Telemetry HUD Overlay
	var hud := CanvasLayer.new()
	hud.name = "HUD"
	main.add_child(hud)
	hud.owner = main

	var panel := PanelContainer.new()
	panel.name = "Panel"
	panel.position = Vector2(18.0, 18.0)
	panel.custom_minimum_size = Vector2(780.0, 118.0)
	hud.add_child(panel)
	panel.owner = main

	var margin := MarginContainer.new()
	margin.name = "Margin"
	margin.add_theme_constant_override(&"margin_left", 12)
	margin.add_theme_constant_override(&"margin_top", 8)
	margin.add_theme_constant_override(&"margin_right", 12)
	margin.add_theme_constant_override(&"margin_bottom", 8)
	panel.add_child(margin)
	margin.owner = main

	var label := RichTextLabel.new()
	label.name = "TelemetryLabel"
	label.bbcode_enabled = true
	label.fit_content = true
	label.scroll_active = false
	label.text = "Initializing Gridlock: Sector 7..."
	margin.add_child(label)
	label.owner = main

	var main_scene := PackedScene.new()
	var pack_err := main_scene.pack(main)
	main.free()
	var save_err := ResourceSaver.save(main_scene, "res://scenes/sector7_main.tscn")
	print("Saved res://scenes/sector7_main.tscn: pack_err=", pack_err, " save_err=", save_err)
	quit(0 if (pack_err == OK and save_err == OK and bake_err == OK) else 1)
