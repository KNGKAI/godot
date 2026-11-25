extends SceneTree

## Builder script for Blackout Protocol (demos/blackout_protocol/).
## Generates the SandboxSchemaRegistry, 3 concurrent SimModules (Weather, Fire, Evacuation),
## Branching Emergency Response Mission & Sequences, 24 Coastal Meso/Macro Partition Cells,
## 400-Agent Hydratable Fire Truck & Evacuation Sedan prefabs, Rescue-1 Helicopter with
## In-Cockpit Diegetic Terminal, and the 3-track FLIR cutscene.

const WeatherSimScript = preload("res://scripts/weather_storm_sim.gd")
const FireSimScript = preload("res://scripts/fire_spread_sim.gd")
const EvacSimScript = preload("res://scripts/evacuation_traffic_sim.gd")
const EmergencyVehicleScript = preload("res://scripts/hydrated_emergency_vehicle.gd")
const ChopperScript = preload("res://scripts/rescue_chopper_controller.gd")
const BlackoutManagerScript = preload("res://scripts/blackout_manager.gd")


func _initialize() -> void:
	_build_all.call_deferred()


func _node_key(id: String) -> String:
	return "node_" + id.to_utf8_buffer().hex_encode()


func _build_all() -> void:
	DirAccess.make_dir_recursive_absolute("res://resources")
	DirAccess.make_dir_recursive_absolute("res://scenes")
	DirAccess.make_dir_recursive_absolute("res://baked_cells/coastal_1")

	# 1. Create & Save SandboxSchemaRegistry
	var schema := SandboxSchemaRegistry.new()
	schema.register_state_key(&"weather.wind_speed_knots", TYPE_FLOAT, 58.0, Vector2(0.0, 120.0), false, "Coastal gale wind speed in knots.")
	schema.register_state_key(&"weather.rain_intensity", TYPE_FLOAT, 0.45, Vector2(0.0, 1.0), false, "Normalized rainfall intensity [0.0, 1.0].")
	schema.register_state_key(&"weather.flood_level_m", TYPE_FLOAT, 3.4, Vector2(0.0, 10.0), false, "Harbor storm surge water elevation in meters.")
	schema.register_state_key(&"weather.seawall_gates_open", TYPE_BOOL, false, Vector2.ZERO, false, "True when harbor diversion floodgates are open.")
	schema.register_state_key(&"fire.city_hazard_pct", TYPE_FLOAT, 78.0, Vector2(0.0, 100.0), false, "Aggregate urban fire hazard percentage (target <= 15%).")
	schema.register_state_key(&"fire.burning_blocks", TYPE_INT, 6, Vector2(0.0, 16.0), false, "Count of active burning city blocks.")
	schema.register_state_key(&"fire.trucks_dispatched", TYPE_BOOL, false, Vector2.ZERO, false, "True when ground foam-suppression fire trucks are deployed.")
	schema.register_state_key(&"fire.airdrop_deployed", TYPE_BOOL, false, Vector2.ZERO, false, "True when Rescue-1 executes an aerial retardant drop.")
	schema.register_state_key(&"evac.contraflow_active", TYPE_BOOL, false, Vector2.ZERO, false, "True when coastal highway lanes are reversed for contraflow evacuation.")
	schema.register_state_key(&"evac.civilians_evacuated_pct", TYPE_FLOAT, 22.0, Vector2(0.0, 100.0), false, "Percentage of coastal district residents evacuated (target >= 90%).")
	schema.register_state_key(&"evac.active_vehicles", TYPE_INT, 400, Vector2(0.0, 2000.0), false, "Total active civilian cars and fire trucks in SoA memory.")
	schema.register_state_key(&"mission.radar_linked", TYPE_BOOL, false, Vector2.ZERO, false, "True once Rescue-1 links its airborne Doppler & FLIR radar.")
	schema.register_state_key(&"mission.emergency_phase", TYPE_STRING, "code_red", Vector2.ZERO, false, "Current coastal emergency response phase.")

	schema.register_event(&"weather.storm_surge_warning", {"flood_level_m": TYPE_FLOAT}, false, "Emitted when harbor storm surge exceeds 3.5m.")
	schema.register_event(&"weather.floodgates_opened", {}, false, "Emitted when coastal floodgates open.")
	schema.register_event(&"fire.trucks_deployed", {"units": TYPE_INT}, false, "Emitted when ground fire brigades deploy.")
	schema.register_event(&"fire.airdrop_executed", {"sector": TYPE_STRING}, false, "Emitted when Rescue-1 executes an aerial water drop.")
	schema.register_event(&"fire.all_contained", {"hazard_pct": TYPE_FLOAT}, false, "Emitted when city fire hazard drops to <= 15%.")
	schema.register_event(&"evac.radar_swept", {"unit": TYPE_STRING}, false, "Emitted when airborne radar sweep completes.")
	schema.register_event(&"evac.contraflow_enabled", {"speed_mps": TYPE_FLOAT}, false, "Emitted when highway contraflow opens.")
	schema.register_event(&"evac.corridor_secured", {"evacuated_pct": TYPE_FLOAT}, false, "Emitted when civilian evacuation reaches >= 90%.")

	schema.register_actor_id(&"rescue_chopper", "Rescue-1 Emergency Command Helicopter.")
	schema.register_actor_id(&"refinery_complex", "Harbor Industrial Refinery sector.")
	schema.register_marker_id(&"MarkerHelipadHQ", "Coastal Dispatch HQ rooftop helipad.")
	schema.register_marker_id(&"MarkerRefineryFire", "Burning Industrial Refinery drop zone.")
	schema.register_marker_id(&"MarkerSeawallControl", "Harbor Seawall Floodgate control tower.")
	schema.register_archetype_id(&"fire_truck", "Hydratable emergency foam response truck.")
	schema.register_archetype_id(&"evac_sedan", "Hydratable civilian evacuation vehicle.")
	schema.register_tag(&"airborne", "Airborne rescue unit.")
	schema.register_tag(&"emergency", "Emergency response vehicle.")

	ResourceSaver.save(schema, "res://resources/sandbox_schema.tres")
	schema = ResourceLoader.load("res://resources/sandbox_schema.tres")

	# 2. Create & Save the 3 Concurrent SimModule Resources
	var weather_mod := SimModule.new()
	weather_mod.set_script(WeatherSimScript)
	weather_mod.module_id = &"weather_storm_sim"
	weather_mod.tick_rate_hz = 10.0
	weather_mod.priority = 0
	ResourceSaver.save(weather_mod, "res://resources/weather_storm_sim.tres")
	weather_mod = ResourceLoader.load("res://resources/weather_storm_sim.tres")

	var fire_mod := SimModule.new()
	fire_mod.set_script(FireSimScript)
	fire_mod.module_id = &"fire_spread_sim"
	fire_mod.tick_rate_hz = 10.0
	fire_mod.priority = 1
	ResourceSaver.save(fire_mod, "res://resources/fire_spread_sim.tres")
	fire_mod = ResourceLoader.load("res://resources/fire_spread_sim.tres")

	var evac_mod := SimModule.new()
	evac_mod.set_script(EvacSimScript)
	evac_mod.module_id = &"evacuation_traffic_sim"
	evac_mod.tick_rate_hz = 5.0
	evac_mod.priority = 2
	ResourceSaver.save(evac_mod, "res://resources/evacuation_traffic_sim.tres")
	evac_mod = ResourceLoader.load("res://resources/evacuation_traffic_sim.tres")

	# 3. Create & Save Lifecycle Sequences
	var seq_start := SequenceData.new()
	seq_start.sequence_id = &"seq_storm_landfall"
	seq_start.entry_step_uuid = &"step_code_red"

	var s1 := ActionSetState.new()
	s1.step_uuid = &"step_code_red"
	s1.key = &"mission.emergency_phase"
	s1.value = "storm_landfall_active"
	var start_steps: Array[SequenceAction] = [s1]
	seq_start.steps = start_steps
	seq_start.set_editor_graph_meta({
		"entry": {"position": Vector2(40, 120)},
		_node_key("step_code_red"): {"position": Vector2(340, 100)}
	})
	ResourceSaver.save(seq_start, "res://resources/seq_storm_landfall.tres")
	seq_start = ResourceLoader.load("res://resources/seq_storm_landfall.tres")

	var seq_complete := SequenceData.new()
	seq_complete.sequence_id = &"seq_all_clear"
	seq_complete.entry_step_uuid = &"step_phase_all_clear"

	var sc1 := ActionSetState.new()
	sc1.step_uuid = &"step_phase_all_clear"
	sc1.key = &"mission.emergency_phase"
	sc1.value = "city_secured_all_clear"
	var comp_steps: Array[SequenceAction] = [sc1]
	seq_complete.steps = comp_steps
	seq_complete.set_editor_graph_meta({
		"entry": {"position": Vector2(40, 120)},
		_node_key("step_phase_all_clear"): {"position": Vector2(340, 100)}
	})
	ResourceSaver.save(seq_complete, "res://resources/seq_all_clear.tres")
	seq_complete = ResourceLoader.load("res://resources/seq_all_clear.tres")

	# 4. Create & Save Branching MissionData (blackout_emergency)
	var mission := MissionData.new()
	mission.mission_id = &"blackout_emergency"
	mission.entry_objective_id = &"obj_radar_sweep"
	mission.use_first_as_entry = false
	mission.on_start_sequence = seq_start
	mission.on_complete_sequence = seq_complete

	var obj1 := ObjectiveData.new()
	obj1.objective_id = &"obj_radar_sweep"
	obj1.title = "1. Link Airborne Doppler & Thermal Radar (Cockpit CLI)"
	obj1.description = "Run 'dispatch.radar_sweep' on the Rescue-1 Cockpit Terminal."
	obj1.required = true
	var c_radar := CondStateEquals.new()
	c_radar.key = &"mission.radar_linked"
	c_radar.expected = true
	obj1.complete_condition = c_radar

	var obj2a := ObjectiveData.new()
	obj2a.objective_id = &"obj_suppress_fires"
	obj2a.title = "2A. [Fire Suppression] Deploy Fire Brigades or Aerial Drop [F2]"
	obj2a.description = "Run 'dispatch.fire_trucks true' or 'dispatch.airdrop' to suppress the refinery fires."
	obj2a.required = false
	var c_trucks := CondStateEquals.new()
	c_trucks.key = &"fire.trucks_dispatched"
	c_trucks.expected = true
	var c_airdrop := CondStateEquals.new()
	c_airdrop.key = &"fire.airdrop_deployed"
	c_airdrop.expected = true

	var obj2b := ObjectiveData.new()
	obj2b.objective_id = &"obj_evac_and_floodgates"
	obj2b.title = "2B. [Civil Defense] Open Highway Contraflow & Seawall Gates [F1/F3]"
	obj2b.description = "Run 'dispatch.contraflow true' and 'weather.floodgate true'."
	obj2b.required = false
	var c_contra := CondStateEquals.new()
	c_contra.key = &"evac.contraflow_active"
	c_contra.expected = true
	var c_gates := CondStateEquals.new()
	c_gates.key = &"weather.seawall_gates_open"
	c_gates.expected = true

	var obj3 := ObjectiveData.new()
	obj3.objective_id = &"obj_secure_coastal_city"
	obj3.title = "3. Contain Fire Hazard (<= 15%) & Complete Evacuation (>= 90%)"
	obj3.description = "Verify coastal fires are contained and civilian evacuation reaches 90%."
	obj3.required = true

	var c_fire_low := CondStateRange.new()
	c_fire_low.key = &"fire.city_hazard_pct"
	c_fire_low.min_val = 0.0
	c_fire_low.max_val = 15.0

	var c_evac_high := CondStateRange.new()
	c_evac_high.key = &"evac.civilians_evacuated_pct"
	c_evac_high.min_val = 90.0
	c_evac_high.max_val = 100.0

	var c_city_safe := CondAll.new()
	var safe_kids: Array[SandboxCondition] = [c_fire_low, c_evac_high]
	c_city_safe.children = safe_kids
	obj3.complete_condition = c_city_safe

	var c_2a_any := CondAny.new()
	var c2a_kids: Array[SandboxCondition] = [c_trucks, c_airdrop, c_city_safe]
	c_2a_any.children = c2a_kids
	obj2a.complete_condition = c_2a_any

	var c_2b_any := CondAny.new()
	var c2b_kids: Array[SandboxCondition] = [c_contra, c_gates, c_city_safe]
	c_2b_any.children = c2b_kids
	obj2b.complete_condition = c_2b_any

	var objs: Array[ObjectiveData] = [obj1, obj2a, obj2b, obj3]
	mission.objectives = objs

	var t1 := MissionTransition.new()
	t1.from_objective_id = &"obj_radar_sweep"
	t1.to_objective_id = &"obj_suppress_fires"

	var t2 := MissionTransition.new()
	t2.from_objective_id = &"obj_radar_sweep"
	t2.to_objective_id = &"obj_evac_and_floodgates"

	var t3 := MissionTransition.new()
	t3.from_objective_id = &"obj_suppress_fires"
	t3.to_objective_id = &"obj_secure_coastal_city"

	var t4 := MissionTransition.new()
	t4.from_objective_id = &"obj_evac_and_floodgates"
	t4.to_objective_id = &"obj_secure_coastal_city"

	var trs: Array[MissionTransition] = [t1, t2, t3, t4]
	mission.transitions = trs
	mission.set_editor_graph_meta({
		"entry": {"position": Vector2(40, 220)},
		"frame_recon": {"rect": Rect2(Vector2(320, 40), Vector2(420, 360)), "title": "Phase 1: Airborne FLIR Recon"},
		"frame_dispatch": {"rect": Rect2(Vector2(780, 20), Vector2(460, 540)), "title": "Phase 2: Fire & Evacuation Dispatch"},
		"frame_secure": {"rect": Rect2(Vector2(1280, 100), Vector2(480, 400)), "title": "Phase 3: City Containment"},
		_node_key("obj_radar_sweep"): {"position": Vector2(360, 120), "frame": &"frame_recon"},
		_node_key("obj_suppress_fires"): {"position": Vector2(820, 80), "frame": &"frame_dispatch"},
		_node_key("obj_evac_and_floodgates"): {"position": Vector2(820, 320), "frame": &"frame_dispatch"},
		_node_key("obj_secure_coastal_city"): {"position": Vector2(1320, 180), "frame": &"frame_secure"}
	})
	ResourceSaver.save(mission, "res://resources/mission_blackout_emergency.tres")
	mission = ResourceLoader.load("res://resources/mission_blackout_emergency.tres")

	# 5. Create Hydratable Fire Truck & Evacuation Sedan Prefabs
	var truck_proxy := BoxMesh.new()
	truck_proxy.size = Vector3(5.4, 2.4, 2.4)
	var truck_mat := StandardMaterial3D.new()
	truck_mat.albedo_color = Color(0.85, 0.14, 0.12)
	truck_mat.roughness = 0.35
	truck_mat.emission_enabled = true
	truck_mat.emission = Color(1.0, 0.2, 0.1)
	truck_mat.emission_energy_multiplier = 0.8
	truck_proxy.material = truck_mat
	ResourceSaver.save(truck_proxy, "res://resources/fire_truck_proxy_mesh.tres")
	truck_proxy = ResourceLoader.load("res://resources/fire_truck_proxy_mesh.tres")

	var sedan_proxy := BoxMesh.new()
	sedan_proxy.size = Vector3(3.8, 1.3, 1.9)
	var sedan_mat := StandardMaterial3D.new()
	sedan_mat.albedo_color = Color(0.75, 0.78, 0.85)
	sedan_mat.emission_enabled = true
	sedan_mat.emission = Color(1.0, 0.85, 0.45)
	sedan_mat.emission_energy_multiplier = 0.7
	sedan_proxy.material = sedan_mat
	ResourceSaver.save(sedan_proxy, "res://resources/evac_sedan_proxy_mesh.tres")
	sedan_proxy = ResourceLoader.load("res://resources/evac_sedan_proxy_mesh.tres")

	var truck_root := Node3D.new()
	truck_root.name = "FireTruck"
	truck_root.set_script(EmergencyVehicleScript)
	var t_hyd := HydratableActor.new()
	t_hyd.name = "Hydratable"
	t_hyd.archetype_id = &"fire_truck"
	t_hyd.hydrate_radius = 130.0
	t_hyd.dehydrate_radius = 160.0
	truck_root.add_child(t_hyd)
	t_hyd.owner = truck_root
	var t_mesh := MeshInstance3D.new()
	t_mesh.name = "BodyMesh"
	t_mesh.mesh = truck_proxy
	truck_root.add_child(t_mesh)
	t_mesh.owner = truck_root
	var t_siren := OmniLight3D.new()
	t_siren.name = "SirenLight"
	t_siren.position = Vector3(0.0, 1.8, 0.0)
	t_siren.light_color = Color(1.0, 0.18, 0.12)
	t_siren.light_energy = 2.2
	t_siren.omni_range = 16.0
	truck_root.add_child(t_siren)
	t_siren.owner = truck_root
	var truck_scene := PackedScene.new()
	truck_scene.pack(truck_root)
	truck_root.free()
	ResourceSaver.save(truck_scene, "res://scenes/fire_truck.tscn")
	truck_scene = ResourceLoader.load("res://scenes/fire_truck.tscn")

	var sedan_root := Node3D.new()
	sedan_root.name = "EvacSedan"
	sedan_root.set_script(EmergencyVehicleScript)
	var s_hyd := HydratableActor.new()
	s_hyd.name = "Hydratable"
	s_hyd.archetype_id = &"evac_sedan"
	s_hyd.hydrate_radius = 110.0
	s_hyd.dehydrate_radius = 140.0
	sedan_root.add_child(s_hyd)
	s_hyd.owner = sedan_root
	var s_mesh := MeshInstance3D.new()
	s_mesh.name = "BodyMesh"
	s_mesh.mesh = sedan_proxy
	sedan_root.add_child(s_mesh)
	s_mesh.owner = sedan_root
	var sedan_scene := PackedScene.new()
	sedan_scene.pack(sedan_root)
	sedan_root.free()
	ResourceSaver.save(sedan_scene, "res://scenes/evac_sedan.tscn")
	sedan_scene = ResourceLoader.load("res://scenes/evac_sedan.tscn")

	# 6. Build 14 Coastal City Meso Cells (256m each, spanning 5.6km along the flight corridor)
	var manifest := WorldPartitionManifest.new()
	var meso_layer := WorldPartitionLayer.new()
	meso_layer.layer_id = &"meso"
	meso_layer.cell_size = 256.0
	meso_layer.load_radius = 640.0
	meso_layer.evict_radius = 960.0
	var layers_arr: Array[WorldPartitionLayer] = [meso_layer]
	manifest.layers = layers_arr

	var tower_mat := StandardMaterial3D.new()
	tower_mat.albedo_color = Color(0.14, 0.19, 0.26)
	tower_mat.roughness = 0.35
	tower_mat.metallic = 0.6

	var tank_mat := StandardMaterial3D.new()
	tank_mat.albedo_color = Color(0.32, 0.24, 0.2)
	tank_mat.roughness = 0.5

	var tower_box := BoxMesh.new()
	tower_box.size = Vector3(28.0, 52.0, 28.0)

	var cells_arr: Array[WorldPartitionCell] = []
	for gx in range(-1, 21):
		var coord := Vector2i(gx, 0)
		var cell_id := "meso_%d_0" % gx
		var cell_root := Node3D.new()
		cell_root.name = "WorldPartitionCell"

		var st := SurfaceTool.new()
		st.begin(Mesh.PRIMITIVE_TRIANGLES)
		st.set_material(tower_mat)

		for b_i in 2:
			var bldg := MeshInstance3D.new()
			bldg.name = "CoastalTower_%d" % b_i
			bldg.mesh = tower_box
			bldg.material_override = tower_mat if (b_i == 0) else tank_mat
			var pos := Vector3(70.0 + float(b_i) * 110.0, 26.0, -55.0 if (b_i == 0) else 55.0)
			bldg.position = pos
			cell_root.add_child(bldg)
			bldg.owner = cell_root
			st.append_from(tower_box, 0, Transform3D(Basis(), pos))

		var proxy_mesh: ArrayMesh = st.commit()
		var proxy_path := "res://baked_cells/coastal_1/%s_proxy.res" % cell_id
		var high_path := "res://baked_cells/coastal_1/%s.scn" % cell_id
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
	var val_err := manifest.validate()
	var manifest_path := "res://baked_cells/coastal_1/manifest.tres"
	ResourceSaver.save(manifest, manifest_path)
	manifest = ResourceLoader.load(manifest_path) as WorldPartitionManifest
	print("Coastal City Manifest generated: validate_err=", val_err, " cells=", cells_arr.size())

	# 7. Build Playable Main Scene (res://scenes/blackout_main.tscn)
	var main := Node3D.new()
	main.name = "BlackoutCoastalCity"
	main.set_script(BlackoutManagerScript)
	main.set("weather_module", weather_mod)
	main.set("fire_module", fire_mod)
	main.set("evac_module", evac_mod)
	main.set("blackout_mission", mission)
	main.set("partition_manifest", manifest)
	main.set("fire_truck_prefab", truck_scene)
	main.set("evac_sedan_prefab", sedan_scene)
	main.set("fire_truck_proxy_mesh", truck_proxy)
	main.set("evac_sedan_proxy_mesh", sedan_proxy)

	# Stormy Coastal Environment
	var world_env := WorldEnvironment.new()
	world_env.name = "WorldEnvironment"
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.03, 0.05, 0.08)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.15, 0.22, 0.32)
	env.ambient_light_energy = 0.85
	env.glow_enabled = true
	world_env.environment = env
	main.add_child(world_env)
	world_env.owner = main

	var storm_light := DirectionalLight3D.new()
	storm_light.name = "StormSkyLight"
	storm_light.rotation_degrees = Vector3(-48.0, -25.0, 0.0)
	storm_light.light_color = Color(0.55, 0.7, 0.9)
	storm_light.light_energy = 0.75
	storm_light.shadow_enabled = true
	main.add_child(storm_light)
	storm_light.owner = main

	# Coastal Evacuation Highway & Harbor Seawall Ground
	var highway := StaticBody3D.new()
	highway.name = "CoastalHighway"
	main.add_child(highway)
	highway.owner = main

	var h_col := CollisionShape3D.new()
	h_col.name = "HighwayCollider"
	var h_box := BoxShape3D.new()
	h_box.size = Vector3(12000.0, 1.0, 400.0)
	h_col.shape = h_box
	h_col.position = Vector3(2500.0, -0.5, 0.0)
	highway.add_child(h_col)
	h_col.owner = main

	var h_mesh_inst := MeshInstance3D.new()
	h_mesh_inst.name = "HighwaySurface"
	var h_mesh := BoxMesh.new()
	h_mesh.size = Vector3(12000.0, 0.1, 48.0)
	var h_mat := StandardMaterial3D.new()
	h_mat.albedo_color = Color(0.08, 0.09, 0.12)
	h_mat.roughness = 0.25
	h_mesh.material = h_mat
	h_mesh_inst.mesh = h_mesh
	h_mesh_inst.position = Vector3(2500.0, 0.0, 0.0)
	highway.add_child(h_mesh_inst)
	h_mesh_inst.owner = main

	# Burning Refinery Sector & Hazard Beacons
	var refinery := Node3D.new()
	refinery.name = "RefineryComplex"
	refinery.position = Vector3(85.0, 0.0, -35.0)
	main.add_child(refinery)
	refinery.owner = main

	var ref_actor := SandboxActor.new()
	ref_actor.name = "SandboxActor"
	ref_actor.actor_id = &"refinery_complex"
	refinery.add_child(ref_actor)
	ref_actor.owner = main

	for i in 4:
		var fire_light := OmniLight3D.new()
		fire_light.name = "FireHazardLight_%d" % i
		fire_light.add_to_group(&"fire_hazard_light")
		fire_light.position = Vector3(float(i) * 35.0, 8.0, -30.0 if (i % 2 == 0) else 30.0)
		fire_light.light_color = Color(1.0, 0.28, 0.08)
		fire_light.light_energy = 3.8
		fire_light.omni_range = 38.0
		main.add_child(fire_light)
		fire_light.owner = main

	# 3D Scene Waypoint Markers
	var m_hq := Marker3D.new()
	m_hq.name = "MarkerHelipadHQ"
	m_hq.position = Vector3(0.0, 18.0, 0.0)
	main.add_child(m_hq)
	m_hq.owner = main

	var m_ref := Marker3D.new()
	m_ref.name = "MarkerRefineryFire"
	m_ref.position = Vector3(85.0, 24.0, -10.0)
	main.add_child(m_ref)
	m_ref.owner = main

	var m_sea := Marker3D.new()
	m_sea.name = "MarkerSeawallControl"
	m_sea.position = Vector3(180.0, 16.0, 12.0)
	main.add_child(m_sea)
	m_sea.owner = main

	# Rescue-1 Helicopter Rig (90 m/s Flight + StreamingInvoker + In-Cockpit TerminalDisplay3D)
	var chopper := CharacterBody3D.new()
	chopper.name = "RescueChopper"
	chopper.set_script(ChopperScript)
	chopper.position = Vector3(0.0, 18.0, 0.0)
	main.add_child(chopper)
	chopper.owner = main

	var c_col := CollisionShape3D.new()
	c_col.name = "Collider"
	var sphere := SphereShape3D.new()
	sphere.radius = 2.0
	c_col.shape = sphere
	chopper.add_child(c_col)
	c_col.owner = main

	var c_cam := Camera3D.new()
	c_cam.name = "Camera3D"
	c_cam.position = Vector3(0.0, 0.6, 0.0)
	c_cam.rotation_degrees = Vector3(-10.0, 0.0, 0.0)
	c_cam.fov = 75.0
	c_cam.current = true
	chopper.add_child(c_cam)
	c_cam.owner = main

	var rotor := MeshInstance3D.new()
	rotor.name = "RotorBlades"
	var r_box := BoxMesh.new()
	r_box.size = Vector3(9.0, 0.08, 0.35)
	rotor.mesh = r_box
	rotor.position = Vector3(0.0, 2.2, -1.0)
	chopper.add_child(rotor)
	rotor.owner = main

	var c_actor := SandboxActor.new()
	c_actor.name = "SandboxActor"
	c_actor.actor_id = &"rescue_chopper"
	c_actor.tags = [&"airborne", &"emergency"]
	chopper.add_child(c_actor)
	c_actor.owner = main

	var c_invoker := StreamingInvoker.new()
	c_invoker.name = "StreamingInvoker"
	c_invoker.lookahead = 3.5
	chopper.add_child(c_invoker)
	c_invoker.owner = main

	# In-Cockpit Diegetic 3D Terminal mounted inside Rescue-1
	var cockpit_term := TerminalDisplay3D.new()
	cockpit_term.name = "ChopperCockpitTerminal"
	cockpit_term.title = "RESCUE-1 TACTICAL MFD [USER: dispatch.radar_sweep, dispatch.airdrop, dispatch.fire_trucks]"
	cockpit_term.position = Vector3(-0.75, -0.35, -1.8)
	cockpit_term.rotation_degrees = Vector3(-12.0, 18.0, 0.0)
	cockpit_term.screen_size = Vector2(1.1, 0.68)
	cockpit_term.permission_tier = CommandBus.PERMISSION_USER
	var cockpit_ns: Array[StringName] = [&"dispatch", &"stream", &"bubble", &"help"]
	cockpit_term.allowed_namespaces = cockpit_ns
	cockpit_term.actor_path = NodePath("../SandboxActor")
	chopper.add_child(cockpit_term)
	cockpit_term.owner = main

	# Rooftop HQ Admin Terminal & Seawall Floodgate Admin Terminal
	var hq_term := TerminalDisplay3D.new()
	hq_term.name = "DispatchHQTerminal"
	hq_term.title = "COASTAL DISPATCH HQ [ADMIN: dispatch.contraflow, sim.speed, sim.rewind]"
	hq_term.position = Vector3(2.2, 18.0, -3.5)
	hq_term.screen_size = Vector2(2.2, 1.3)
	hq_term.permission_tier = CommandBus.PERMISSION_ADMIN
	hq_term.actor_path = NodePath("../RescueChopper/SandboxActor")
	main.add_child(hq_term)
	hq_term.owner = main

	var seawall_term := TerminalDisplay3D.new()
	seawall_term.name = "SeawallTerminal"
	seawall_term.title = "HARBOR SEAWALL CONTROL [ADMIN: weather.floodgate]"
	seawall_term.position = Vector3(180.0, 16.0, 8.0)
	seawall_term.screen_size = Vector2(2.2, 1.3)
	seawall_term.permission_tier = CommandBus.PERMISSION_ADMIN
	seawall_term.actor_path = NodePath("../RescueChopper/SandboxActor")
	main.add_child(seawall_term)
	seawall_term.owner = main

	# 3-Track Cutscene AnimationPlayer (city_saved_cutscene)
	var anim_player := AnimationPlayer.new()
	anim_player.name = "CutsceneAnimationPlayer"
	var anim := Animation.new()
	anim.length = 2.0

	var tr_state := anim.add_track(Animation.TYPE_WORLD_STATE)
	anim.track_set_path(tr_state, NodePath("WorldState:weather.rain_intensity"))
	anim.track_insert_key(tr_state, 0.0, 0.45)
	anim.track_insert_key(tr_state, 1.5, 0.1)

	var tr_event := anim.add_track(Animation.TYPE_EVENT_BUS)
	anim.track_set_path(tr_event, NodePath("EventBus:evac.corridor_secured"))
	anim.event_bus_track_insert_key(tr_event, 0.2, &"evac.corridor_secured", {"cutscene": true})

	var tr_cmd := anim.add_track(Animation.TYPE_ACTOR_COMMAND)
	anim.track_set_path(tr_cmd, NodePath("ActorRegistry:rescue_chopper"))
	anim.actor_command_track_insert_key(tr_cmd, 0.05, &"rescue_chopper", &"chopper.flir_zoom", {"fov": 46.0, "marker_id": &"MarkerRefineryFire"})

	var lib := AnimationLibrary.new()
	lib.add_animation(&"city_saved_cutscene", anim)
	anim_player.add_animation_library(&"", lib)
	main.add_child(anim_player)
	anim_player.owner = main

	# 2D Telemetry HUD Overlay
	var hud := CanvasLayer.new()
	hud.name = "HUD"
	main.add_child(hud)
	hud.owner = main

	var panel := PanelContainer.new()
	panel.name = "Panel"
	panel.position = Vector2(18.0, 18.0)
	panel.custom_minimum_size = Vector2(920.0, 118.0)
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
	label.text = "Initializing Blackout Protocol..."
	margin.add_child(label)
	label.owner = main

	var main_scene := PackedScene.new()
	var pack_err := main_scene.pack(main)
	main.free()
	var save_err := ResourceSaver.save(main_scene, "res://scenes/blackout_main.tscn")
	print("Saved res://scenes/blackout_main.tscn: pack_err=", pack_err, " save_err=", save_err)
	quit(0 if (pack_err == OK and save_err == OK and val_err == OK) else 1)
