extends SceneTree

## Builder script for Outpost Meridian (demos/outpost_meridian/).
## Generates the SandboxSchemaRegistry, Terraforming & Mining SimModules,
## Branching Mission & Sequences, 16km Macro Crater WorldPartitionManifest,
## Hydratable Ore Hauler & Survey Drone prefabs, Dual 1st-Person + Orbital RTS rigs,
## and the 3-track custom AnimationPlayer cutscene driving both domains simultaneously.

const ColonySimScript = preload("res://scripts/terraforming_colony_sim.gd")
const FleetSimScript = preload("res://scripts/mining_fleet_sim.gd")
const HaulerScript = preload("res://scripts/hydrated_hauler_controller.gd")
const CommanderFpScript = preload("res://scripts/commander_rig_controller.gd")
const OrbitalRtsScript = preload("res://scripts/orbital_rts_controller.gd")
const MeridianManagerScript = preload("res://scripts/meridian_manager.gd")


func _initialize() -> void:
	_build_all.call_deferred()


func _node_key(id: String) -> String:
	return "node_" + id.to_utf8_buffer().hex_encode()


func _build_all() -> void:
	DirAccess.make_dir_recursive_absolute("res://resources")
	DirAccess.make_dir_recursive_absolute("res://scenes")
	DirAccess.make_dir_recursive_absolute("res://baked_cells/macro_1")

	# 1. Create & Save SandboxSchemaRegistry
	var schema := SandboxSchemaRegistry.new()
	schema.register_state_key(&"colony.o2_pressure_kpa", TYPE_FLOAT, 68.0, Vector2(0.0, 120.0), false, "Habitat atmospheric O2 partial pressure in kPa (target >= 95 kPa).")
	schema.register_state_key(&"colony.battery_mwh", TYPE_FLOAT, 140.0, Vector2(0.0, 500.0), false, "Colony solar capacitor reserve in MWh.")
	schema.register_state_key(&"colony.refined_ore_tons", TYPE_FLOAT, 45.0, Vector2(0.0, 1000.0), false, "Processed regolith titanium/ice ore in metric tons (target >= 250 t).")
	schema.register_state_key(&"colony.scrubber_overdrive", TYPE_BOOL, false, Vector2.ZERO, false, "True when atmospheric O2 scrubbers are running in high-output overdrive.")
	schema.register_state_key(&"colony.storm_shield_active", TYPE_BOOL, false, Vector2.ZERO, false, "True when electromagnetic dust shields are active.")
	schema.register_state_key(&"colony.dome_beacon_energy", TYPE_FLOAT, 0.5, Vector2(0.0, 3.0), false, "Habitat dome telemetry beacon brightness.")
	schema.register_state_key(&"fleet.convoy_overclock", TYPE_BOOL, false, Vector2.ZERO, false, "True when crater mining haulers are running at high-speed convoy overclock.")
	schema.register_state_key(&"fleet.hauler_speed_mps", TYPE_FLOAT, 10.0, Vector2(0.0, 45.0), false, "Target speed of autonomous regolith haulers in m/s.")
	schema.register_state_key(&"fleet.active_units", TYPE_INT, 300, Vector2(0.0, 2000.0), false, "Total simulated mining haulers and survey drones in the crater.")
	schema.register_state_key(&"mission.telemetry_linked", TYPE_BOOL, false, Vector2.ZERO, false, "True once orbital crater telemetry is synchronized at the Command Deck.")
	schema.register_state_key(&"mission.expansion_status", TYPE_STRING, "initializing", Vector2.ZERO, false, "Current phase of the Outpost Meridian expansion protocol.")

	schema.register_event(&"colony.telemetry_linked", {"crater": TYPE_STRING}, false, "Emitted when orbital telemetry is synchronized.")
	schema.register_event(&"colony.scrubber_boosted", {"overdrive": TYPE_BOOL}, false, "Emitted when O2 scrubbers enter overdrive.")
	schema.register_event(&"colony.convoy_recalled", {"speed_mps": TYPE_FLOAT}, false, "Emitted when hauler fleet enters convoy overclock.")
	schema.register_event(&"colony.quota_reached", {"o2_kpa": TYPE_FLOAT, "ore_tons": TYPE_FLOAT}, false, "Emitted when colony O2 >= 95 kPa and refined ore >= 250 tons.")
	schema.register_event(&"fleet.origin_crossed", {"shifts": TYPE_INT}, false, "Emitted when the rover crosses a 2048m OriginShifter threshold.")

	schema.register_actor_id(&"commander_fp", "1st-Person / Rover Commander Rig (Domain A).")
	schema.register_actor_id(&"orbital_rts", "Top-Down Orbital RTS Command Rig (Domain B).")
	schema.register_actor_id(&"habitat_core", "Central Meridian Habitat Dome.")
	schema.register_marker_id(&"MarkerCommandDeck", "Habitat Command Deck console waypoint.")
	schema.register_marker_id(&"MarkerScrubberBay", "Atmospheric Scrubber Bay terminal waypoint.")
	schema.register_marker_id(&"MarkerRoverGarage", "Crater Rover bay & telemetry rewind waypoint.")
	schema.register_archetype_id(&"ore_hauler", "Autonomous 6-wheel regolith hauler.")
	schema.register_archetype_id(&"survey_drone", "Low-altitude crater survey skimmer.")
	schema.register_tag(&"commander", "Active player perspective rig.")
	schema.register_tag(&"harvester", "Autonomous mining fleet unit.")

	ResourceSaver.save(schema, "res://resources/sandbox_schema.tres")
	schema = ResourceLoader.load("res://resources/sandbox_schema.tres")

	# 2. Create & Save SimModule Resources
	var colony_mod := SimModule.new()
	colony_mod.set_script(ColonySimScript)
	colony_mod.module_id = &"terraforming_colony_sim"
	colony_mod.tick_rate_hz = 10.0
	ResourceSaver.save(colony_mod, "res://resources/terraforming_colony_sim.tres")
	colony_mod = ResourceLoader.load("res://resources/terraforming_colony_sim.tres")

	var fleet_mod := SimModule.new()
	fleet_mod.set_script(FleetSimScript)
	fleet_mod.module_id = &"mining_fleet_sim"
	fleet_mod.tick_rate_hz = 5.0
	ResourceSaver.save(fleet_mod, "res://resources/mining_fleet_sim.tres")
	fleet_mod = ResourceLoader.load("res://resources/mining_fleet_sim.tres")

	# 3. Create & Save Lifecycle Sequences
	var seq_start := SequenceData.new()
	seq_start.sequence_id = &"seq_meridian_boot"
	seq_start.entry_step_uuid = &"step_status_boot"

	var s1 := ActionSetState.new()
	s1.step_uuid = &"step_status_boot"
	s1.key = &"mission.expansion_status"
	s1.value = "awaiting_telemetry"
	var start_steps: Array[SequenceAction] = [s1]
	seq_start.steps = start_steps
	seq_start.set_editor_graph_meta({
		"entry": {"position": Vector2(40, 120)},
		_node_key("step_status_boot"): {"position": Vector2(340, 100)}
	})
	ResourceSaver.save(seq_start, "res://resources/seq_meridian_boot.tres")
	seq_start = ResourceLoader.load("res://resources/seq_meridian_boot.tres")

	var seq_complete := SequenceData.new()
	seq_complete.sequence_id = &"seq_meridian_terraformed"
	seq_complete.entry_step_uuid = &"step_mark_complete"

	var sc1 := ActionSetState.new()
	sc1.step_uuid = &"step_mark_complete"
	sc1.key = &"mission.expansion_status"
	sc1.value = "sector_terraformed"
	sc1.next_steps = PackedStringArray(["step_beacon_full"])

	var sc2 := ActionSetState.new()
	sc2.step_uuid = &"step_beacon_full"
	sc2.key = &"colony.dome_beacon_energy"
	sc2.value = 2.5

	var comp_steps: Array[SequenceAction] = [sc1, sc2]
	seq_complete.steps = comp_steps
	seq_complete.set_editor_graph_meta({
		"entry": {"position": Vector2(40, 120)},
		_node_key("step_mark_complete"): {"position": Vector2(340, 100)},
		_node_key("step_beacon_full"): {"position": Vector2(660, 100)}
	})
	ResourceSaver.save(seq_complete, "res://resources/seq_meridian_terraformed.tres")
	seq_complete = ResourceLoader.load("res://resources/seq_meridian_terraformed.tres")

	# 4. Create & Save Branching MissionData (meridian_expansion)
	var mission := MissionData.new()
	mission.mission_id = &"meridian_expansion"
	mission.entry_objective_id = &"obj_link_telemetry"
	mission.use_first_as_entry = false
	mission.on_start_sequence = seq_start
	mission.on_complete_sequence = seq_complete

	var obj1 := ObjectiveData.new()
	obj1.objective_id = &"obj_link_telemetry"
	obj1.title = "1. Synchronize Orbital Telemetry at Command Deck [F1]"
	obj1.description = "Run 'colony.link_telemetry' on the Command Deck terminal."
	obj1.required = true
	var c_linked := CondStateEquals.new()
	c_linked.key = &"mission.telemetry_linked"
	c_linked.expected = true
	obj1.complete_condition = c_linked

	var obj2a := ObjectiveData.new()
	obj2a.objective_id = &"obj_boost_scrubbers"
	obj2a.title = "2A. [Atmosphere Branch] Activate O2 Scrubber Overdrive [F2]"
	obj2a.description = "Run 'colony.scrubber_boost true' on the Scrubber Bay terminal."
	obj2a.required = false
	var c_scrub := CondStateEquals.new()
	c_scrub.key = &"colony.scrubber_overdrive"
	c_scrub.expected = true
	obj2a.complete_condition = c_scrub

	var obj2b := ObjectiveData.new()
	obj2b.objective_id = &"obj_overclock_fleet"
	obj2b.title = "2B. [Fleet Branch] Overclock Crater Mining Fleet [F1]"
	obj2b.description = "Run 'fleet.overclock true' on the Command Deck Admin Console."
	obj2b.required = false
	var c_fleet := CondStateEquals.new()
	c_fleet.key = &"fleet.convoy_overclock"
	c_fleet.expected = true
	obj2b.complete_condition = c_fleet

	var obj3 := ObjectiveData.new()
	obj3.objective_id = &"obj_reach_colony_quota"
	obj3.title = "3. Reach Terraforming Quota (O2 >= 95 kPa & Ore >= 250 t) [F4: 50x SimClock]"
	obj3.description = "Fast-forward SimClock up to 50x ('sim.speed 50' or [F4]) until O2 and Ore reach target thresholds."
	obj3.required = true

	var c_o2 := CondStateRange.new()
	c_o2.key = &"colony.o2_pressure_kpa"
	c_o2.min_val = 95.0
	c_o2.max_val = 120.0

	var c_ore := CondStateRange.new()
	c_ore.key = &"colony.refined_ore_tons"
	c_ore.min_val = 250.0
	c_ore.max_val = 1000.0

	var c_quota_all := CondAll.new()
	var q_children: Array[SandboxCondition] = [c_o2, c_ore]
	c_quota_all.children = q_children
	obj3.complete_condition = c_quota_all

	# Allow either optional branch to auto-clear once final quota is reached
	var c_2a_any := CondAny.new()
	var c2a_kids: Array[SandboxCondition] = [c_scrub, c_quota_all]
	c_2a_any.children = c2a_kids
	obj2a.complete_condition = c_2a_any

	var c_2b_any := CondAny.new()
	var c2b_kids: Array[SandboxCondition] = [c_fleet, c_quota_all]
	c_2b_any.children = c2b_kids
	obj2b.complete_condition = c_2b_any

	var objs: Array[ObjectiveData] = [obj1, obj2a, obj2b, obj3]
	mission.objectives = objs

	var t1 := MissionTransition.new()
	t1.from_objective_id = &"obj_link_telemetry"
	t1.to_objective_id = &"obj_boost_scrubbers"

	var t2 := MissionTransition.new()
	t2.from_objective_id = &"obj_link_telemetry"
	t2.to_objective_id = &"obj_overclock_fleet"

	var t3 := MissionTransition.new()
	t3.from_objective_id = &"obj_boost_scrubbers"
	t3.to_objective_id = &"obj_reach_colony_quota"

	var t4 := MissionTransition.new()
	t4.from_objective_id = &"obj_overclock_fleet"
	t4.to_objective_id = &"obj_reach_colony_quota"

	var trs: Array[MissionTransition] = [t1, t2, t3, t4]
	mission.transitions = trs
	mission.set_editor_graph_meta({
		"entry": {"position": Vector2(40, 220)},
		"frame_link": {"rect": Rect2(Vector2(320, 40), Vector2(420, 360)), "title": "Phase 1: Orbital Telemetry"},
		"frame_ops": {"rect": Rect2(Vector2(780, 20), Vector2(460, 540)), "title": "Phase 2: Colony & Fleet Directives"},
		"frame_quota": {"rect": Rect2(Vector2(1280, 100), Vector2(480, 400)), "title": "Phase 3: 50x Fast-Forward Quota"},
		_node_key("obj_link_telemetry"): {"position": Vector2(360, 120), "frame": &"frame_link"},
		_node_key("obj_boost_scrubbers"): {"position": Vector2(820, 80), "frame": &"frame_ops"},
		_node_key("obj_overclock_fleet"): {"position": Vector2(820, 320), "frame": &"frame_ops"},
		_node_key("obj_reach_colony_quota"): {"position": Vector2(1320, 180), "frame": &"frame_quota"}
	})
	ResourceSaver.save(mission, "res://resources/mission_meridian_expansion.tres")
	mission = ResourceLoader.load("res://resources/mission_meridian_expansion.tres")

	# 5. Create Hydratable Ore Hauler & Survey Drone Prefabs
	var hauler_proxy := BoxMesh.new()
	hauler_proxy.size = Vector3(4.6, 2.0, 2.6)
	var hauler_mat := StandardMaterial3D.new()
	hauler_mat.albedo_color = Color(0.75, 0.52, 0.14)
	hauler_mat.roughness = 0.55
	hauler_mat.metallic = 0.45
	hauler_proxy.material = hauler_mat
	ResourceSaver.save(hauler_proxy, "res://resources/hauler_proxy_mesh.tres")
	hauler_proxy = ResourceLoader.load("res://resources/hauler_proxy_mesh.tres")

	var drone_proxy := SphereMesh.new()
	drone_proxy.radius = 0.75
	drone_proxy.height = 1.2
	var drone_mat := StandardMaterial3D.new()
	drone_mat.albedo_color = Color(0.85, 0.9, 0.95)
	drone_mat.emission_enabled = true
	drone_mat.emission = Color(0.2, 0.85, 1.0)
	drone_proxy.material = drone_mat
	ResourceSaver.save(drone_proxy, "res://resources/survey_drone_proxy_mesh.tres")
	drone_proxy = ResourceLoader.load("res://resources/survey_drone_proxy_mesh.tres")

	var hauler_root := Node3D.new()
	hauler_root.name = "OreHauler"
	hauler_root.set_script(HaulerScript)
	var h_hyd := HydratableActor.new()
	h_hyd.name = "Hydratable"
	h_hyd.archetype_id = &"ore_hauler"
	h_hyd.hydrate_radius = 110.0
	h_hyd.dehydrate_radius = 140.0
	hauler_root.add_child(h_hyd)
	h_hyd.owner = hauler_root
	var h_mesh := MeshInstance3D.new()
	h_mesh.name = "BodyMesh"
	h_mesh.mesh = hauler_proxy
	hauler_root.add_child(h_mesh)
	h_mesh.owner = hauler_root
	var h_light := OmniLight3D.new()
	h_light.name = "StatusLight"
	h_light.position = Vector3(2.4, 1.2, 0.0)
	h_light.light_color = Color(1.0, 0.78, 0.2)
	h_light.light_energy = 1.8
	h_light.omni_range = 14.0
	hauler_root.add_child(h_light)
	h_light.owner = hauler_root
	var hauler_scene := PackedScene.new()
	hauler_scene.pack(hauler_root)
	hauler_root.free()
	ResourceSaver.save(hauler_scene, "res://scenes/ore_hauler.tscn")
	hauler_scene = ResourceLoader.load("res://scenes/ore_hauler.tscn")

	var drone_root := Node3D.new()
	drone_root.name = "SurveyDrone"
	drone_root.set_script(HaulerScript)
	var d_hyd := HydratableActor.new()
	d_hyd.name = "Hydratable"
	d_hyd.archetype_id = &"survey_drone"
	d_hyd.hydrate_radius = 80.0
	d_hyd.dehydrate_radius = 105.0
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
	ResourceSaver.save(drone_scene, "res://scenes/survey_drone.tscn")
	drone_scene = ResourceLoader.load("res://scenes/survey_drone.tscn")

	# 6. Build 16 Macro Crater Outpost Cells (512m each, spanning 8km corridor across OriginShifter boundaries)
	var manifest := WorldPartitionManifest.new()
	var macro_layer := WorldPartitionLayer.new()
	macro_layer.layer_id = &"macro"
	macro_layer.cell_size = 512.0
	macro_layer.load_radius = 1024.0
	macro_layer.evict_radius = 1536.0
	var layers_arr: Array[WorldPartitionLayer] = [macro_layer]
	manifest.layers = layers_arr

	var outpost_mat := StandardMaterial3D.new()
	outpost_mat.albedo_color = Color(0.62, 0.36, 0.22)
	outpost_mat.roughness = 0.75

	var solar_mat := StandardMaterial3D.new()
	solar_mat.albedo_color = Color(0.12, 0.25, 0.48)
	solar_mat.metallic = 0.8
	solar_mat.roughness = 0.2

	var ridge_box := BoxMesh.new()
	ridge_box.size = Vector3(45.0, 26.0, 45.0)
	var solar_box := BoxMesh.new()
	solar_box.size = Vector3(32.0, 4.0, 18.0)

	var cells_arr: Array[WorldPartitionCell] = []
	for gx in range(-1, 11):
		var coord := Vector2i(gx, 0)
		var cell_id := "macro_%d_0" % gx
		var cell_root := Node3D.new()
		cell_root.name = "WorldPartitionCell"

		var st := SurfaceTool.new()
		st.begin(Mesh.PRIMITIVE_TRIANGLES)
		st.set_material(outpost_mat)

		for idx in 2:
			var spire := MeshInstance3D.new()
			spire.name = "CraterRidge_%d" % idx
			spire.mesh = ridge_box
			spire.material_override = outpost_mat
			var pos := Vector3(120.0 + float(idx) * 220.0, 13.0, -75.0 if idx == 0 else 75.0)
			spire.position = pos
			cell_root.add_child(spire)
			spire.owner = cell_root
			st.append_from(ridge_box, 0, Transform3D(Basis(), pos))

			var panel_inst := MeshInstance3D.new()
			panel_inst.name = "SolarRelay_%d" % idx
			panel_inst.mesh = solar_box
			panel_inst.material_override = solar_mat
			panel_inst.position = Vector3(160.0 + float(idx) * 180.0, 2.0, -38.0 if idx == 0 else 38.0)
			cell_root.add_child(panel_inst)
			panel_inst.owner = cell_root

		var proxy_mesh: ArrayMesh = st.commit()
		var proxy_path := "res://baked_cells/macro_1/%s_proxy.res" % cell_id
		var high_path := "res://baked_cells/macro_1/%s.scn" % cell_id
		ResourceSaver.save(proxy_mesh, proxy_path, ResourceSaver.FLAG_COMPRESS)

		var cell_scene := PackedScene.new()
		cell_scene.pack(cell_root)
		cell_root.free()
		ResourceSaver.save(cell_scene, high_path, ResourceSaver.FLAG_COMPRESS)

		var wp_cell := WorldPartitionCell.new()
		wp_cell.cell_id = StringName(cell_id)
		wp_cell.layer = &"macro"
		wp_cell.grid_coords = coord
		wp_cell.hlod_impostor_path = proxy_path
		wp_cell.high_scene_path = high_path
		cells_arr.append(wp_cell)

	manifest.cells = cells_arr
	var val_err := manifest.validate()
	var manifest_path := "res://baked_cells/macro_1/manifest.tres"
	ResourceSaver.save(manifest, manifest_path)
	manifest = ResourceLoader.load(manifest_path) as WorldPartitionManifest
	print("Crater Macro Manifest generated: validate_err=", val_err, " cells=", cells_arr.size())

	# 7. Build Playable Main Scene (res://scenes/meridian_main.tscn)
	var main := Node3D.new()
	main.name = "OutpostMeridian"
	main.set_script(MeridianManagerScript)
	main.set("colony_module", colony_mod)
	main.set("fleet_module", fleet_mod)
	main.set("meridian_mission", mission)
	main.set("partition_manifest", manifest)
	main.set("hauler_prefab", hauler_scene)
	main.set("drone_prefab", drone_scene)
	main.set("hauler_proxy_mesh", hauler_proxy)
	main.set("drone_proxy_mesh", drone_proxy)

	# Planetary Atmosphere Environment
	var world_env := WorldEnvironment.new()
	world_env.name = "WorldEnvironment"
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.18, 0.09, 0.05)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.45, 0.28, 0.2)
	env.ambient_light_energy = 0.9
	world_env.environment = env
	main.add_child(world_env)
	world_env.owner = main

	var sun := DirectionalLight3D.new()
	sun.name = "CraterSun"
	sun.rotation_degrees = Vector3(-35.0, 25.0, 0.0)
	sun.light_color = Color(1.0, 0.85, 0.72)
	sun.light_energy = 1.35
	sun.shadow_enabled = true
	main.add_child(sun)
	sun.owner = main

	# Regolith Floor Collider & Visual Mining Highway
	var ground_body := StaticBody3D.new()
	ground_body.name = "CraterFloor"
	main.add_child(ground_body)
	ground_body.owner = main

	var ground_col := CollisionShape3D.new()
	ground_col.name = "FloorCollider"
	var ground_box := BoxShape3D.new()
	ground_box.size = Vector3(12000.0, 1.0, 600.0)
	ground_col.shape = ground_box
	ground_col.position = Vector3(2500.0, -0.5, 0.0)
	ground_body.add_child(ground_col)
	ground_col.owner = main

	var regolith_inst := MeshInstance3D.new()
	regolith_inst.name = "RegolithSurface"
	var regolith_mesh := BoxMesh.new()
	regolith_mesh.size = Vector3(12000.0, 0.1, 200.0)
	var regolith_mat := StandardMaterial3D.new()
	regolith_mat.albedo_color = Color(0.42, 0.22, 0.14)
	regolith_mat.roughness = 0.85
	regolith_mesh.material = regolith_mat
	regolith_inst.mesh = regolith_mesh
	regolith_inst.position = Vector3(2500.0, 0.0, 0.0)
	ground_body.add_child(regolith_inst)
	regolith_inst.owner = main

	# Central Habitat Dome & Telemetry Beacon
	var dome := Node3D.new()
	dome.name = "HabitatCore"
	dome.position = Vector3(0.0, 0.0, -22.0)
	main.add_child(dome)
	dome.owner = main

	var dome_actor := SandboxActor.new()
	dome_actor.name = "SandboxActor"
	dome_actor.actor_id = &"habitat_core"
	dome.add_child(dome_actor)
	dome_actor.owner = main

	var dome_mesh_inst := MeshInstance3D.new()
	dome_mesh_inst.name = "DomeMesh"
	var dome_sphere := SphereMesh.new()
	dome_sphere.radius = 10.0
	dome_sphere.height = 10.0
	dome_sphere.is_hemisphere = true
	var dome_mat := StandardMaterial3D.new()
	dome_mat.albedo_color = Color(0.75, 0.82, 0.88)
	dome_mat.metallic = 0.5
	dome_mat.roughness = 0.25
	dome_sphere.material = dome_mat
	dome_mesh_inst.mesh = dome_sphere
	dome.add_child(dome_mesh_inst)
	dome_mesh_inst.owner = main

	var beacon := OmniLight3D.new()
	beacon.name = "DomeBeacon"
	beacon.position = Vector3(0.0, 12.0, 0.0)
	beacon.light_color = Color(1.0, 0.55, 0.15)
	beacon.light_energy = 2.0
	beacon.omni_range = 45.0
	dome.add_child(beacon)
	beacon.owner = main

	# 3D Scene Waypoint Markers
	var m_cmd := Marker3D.new()
	m_cmd.name = "MarkerCommandDeck"
	m_cmd.position = Vector3(0.0, 1.1, -6.0)
	main.add_child(m_cmd)
	m_cmd.owner = main

	var m_scrub := Marker3D.new()
	m_scrub.name = "MarkerScrubberBay"
	m_scrub.position = Vector3(-12.0, 1.1, -4.0)
	main.add_child(m_scrub)
	m_scrub.owner = main

	var m_rover := Marker3D.new()
	m_rover.name = "MarkerRoverGarage"
	m_rover.position = Vector3(12.0, 1.1, -4.0)
	main.add_child(m_rover)
	m_rover.owner = main

	# Domain A: First-Person / High-Speed Rover Commander Rig
	var fp_rig := CharacterBody3D.new()
	fp_rig.name = "CommanderFP"
	fp_rig.set_script(CommanderFpScript)
	fp_rig.position = Vector3(0.0, 1.1, -3.0)
	main.add_child(fp_rig)
	fp_rig.owner = main

	var fp_col := CollisionShape3D.new()
	fp_col.name = "Collider"
	var capsule := CapsuleShape3D.new()
	capsule.radius = 0.4
	capsule.height = 1.8
	fp_col.shape = capsule
	fp_rig.add_child(fp_col)
	fp_col.owner = main

	var fp_cam := Camera3D.new()
	fp_cam.name = "Camera3D"
	fp_cam.position = Vector3(0.0, 0.65, 0.0)
	fp_cam.fov = 75.0
	fp_cam.current = true
	fp_rig.add_child(fp_cam)
	fp_cam.owner = main

	var fp_actor := SandboxActor.new()
	fp_actor.name = "SandboxActor"
	fp_actor.actor_id = &"commander_fp"
	fp_actor.tags = [&"commander"]
	fp_rig.add_child(fp_actor)
	fp_actor.owner = main

	var fp_invoker := StreamingInvoker.new()
	fp_invoker.name = "StreamingInvoker"
	fp_invoker.lookahead = 3.5
	fp_rig.add_child(fp_invoker)
	fp_invoker.owner = main

	# Domain B: Top-Down Orbital RTS Command Rig (Orthographic Camera3D + SandboxActor)
	var rts_rig := Node3D.new()
	rts_rig.name = "OrbitalRTS"
	rts_rig.set_script(OrbitalRtsScript)
	rts_rig.position = Vector3(0.0, 55.0, -3.0)
	main.add_child(rts_rig)
	rts_rig.owner = main

	var rts_cam := Camera3D.new()
	rts_cam.name = "Camera3D"
	rts_cam.projection = Camera3D.PROJECTION_ORTHOGONAL
	rts_cam.size = 42.0
	rts_cam.rotation_degrees = Vector3(-90.0, 0.0, 0.0)
	rts_cam.current = false
	rts_rig.add_child(rts_cam)
	rts_cam.owner = main

	var rts_actor := SandboxActor.new()
	rts_actor.name = "SandboxActor"
	rts_actor.actor_id = &"orbital_rts"
	rts_actor.tags = [&"commander"]
	rts_rig.add_child(rts_actor)
	rts_actor.owner = main

	var rts_invoker := StreamingInvoker.new()
	rts_invoker.name = "StreamingInvoker"
	rts_invoker.lookahead = 2.0
	rts_rig.add_child(rts_invoker)
	rts_invoker.owner = main

	# Shared 3-Track Cutscene AnimationPlayer driving BOTH Domain A (commander_fp) and Domain B (orbital_rts)
	var anim_player := AnimationPlayer.new()
	anim_player.name = "CutsceneAnimationPlayer"
	var anim := Animation.new()
	anim.length = 2.0

	var tr_state := anim.add_track(Animation.TYPE_WORLD_STATE)
	anim.track_set_path(tr_state, NodePath("WorldState:colony.dome_beacon_energy"))
	anim.track_insert_key(tr_state, 0.0, 0.6)
	anim.track_insert_key(tr_state, 1.5, 2.5)

	var tr_event := anim.add_track(Animation.TYPE_EVENT_BUS)
	anim.track_set_path(tr_event, NodePath("EventBus:colony.telemetry_linked"))
	anim.event_bus_track_insert_key(tr_event, 0.2, &"colony.telemetry_linked", {"cutscene": true})

	var tr_cmd_fp := anim.add_track(Animation.TYPE_ACTOR_COMMAND)
	anim.track_set_path(tr_cmd_fp, NodePath("ActorRegistry:commander_fp"))
	anim.actor_command_track_insert_key(tr_cmd_fp, 0.05, &"commander_fp", &"camera.cutscene_zoom", {"fov": 48.0, "marker_id": &"MarkerCommandDeck"})

	var tr_cmd_rts := anim.add_track(Animation.TYPE_ACTOR_COMMAND)
	anim.track_set_path(tr_cmd_rts, NodePath("ActorRegistry:orbital_rts"))
	anim.actor_command_track_insert_key(tr_cmd_rts, 0.05, &"orbital_rts", &"camera.cutscene_zoom", {"ortho_size": 20.0, "marker_id": &"MarkerCommandDeck"})

	var lib := AnimationLibrary.new()
	lib.add_animation(&"colony_quota_cutscene", anim)
	anim_player.add_animation_library(&"", lib)
	main.add_child(anim_player)
	anim_player.owner = main

	# Three Diegetic 3D Terminals
	var cmd_term := TerminalDisplay3D.new()
	cmd_term.name = "CommandDeckTerminal"
	cmd_term.title = "ORBITAL COMMAND DECK [ADMIN: colony.*, fleet.*, view.*, sim.*]"
	cmd_term.position = Vector3(0.0, 1.65, -9.0)
	cmd_term.screen_size = Vector2(2.4, 1.4)
	cmd_term.permission_tier = CommandBus.PERMISSION_ADMIN
	cmd_term.actor_path = NodePath("../CommanderFP/SandboxActor")
	main.add_child(cmd_term)
	cmd_term.owner = main

	var scrub_term := TerminalDisplay3D.new()
	scrub_term.name = "ScrubberBayTerminal"
	scrub_term.title = "ATMOSPHERIC SCRUBBER BAY [USER: colony.scrubber_boost]"
	scrub_term.position = Vector3(-12.0, 1.65, -7.0)
	scrub_term.screen_size = Vector2(2.0, 1.2)
	scrub_term.permission_tier = CommandBus.PERMISSION_USER
	var scrub_ns: Array[StringName] = [&"colony", &"help"]
	scrub_term.allowed_namespaces = scrub_ns
	scrub_term.actor_path = NodePath("../CommanderFP/SandboxActor")
	main.add_child(scrub_term)
	scrub_term.owner = main

	var rewind_term := TerminalDisplay3D.new()
	rewind_term.name = "RoverTelemetryTerminal"
	rewind_term.title = "PREDICTIVE TELEMETRY & ROVER BAY [ADMIN: sim.rewind, sim.speed]"
	rewind_term.position = Vector3(12.0, 1.65, -7.0)
	rewind_term.screen_size = Vector2(2.1, 1.25)
	rewind_term.permission_tier = CommandBus.PERMISSION_ADMIN
	rewind_term.actor_path = NodePath("../CommanderFP/SandboxActor")
	main.add_child(rewind_term)
	rewind_term.owner = main

	# 2D Telemetry HUD Overlay
	var hud := CanvasLayer.new()
	hud.name = "HUD"
	main.add_child(hud)
	hud.owner = main

	var panel := PanelContainer.new()
	panel.name = "Panel"
	panel.position = Vector2(18.0, 18.0)
	panel.custom_minimum_size = Vector2(860.0, 118.0)
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
	label.text = "Initializing Outpost Meridian..."
	margin.add_child(label)
	label.owner = main

	var main_scene := PackedScene.new()
	var pack_err := main_scene.pack(main)
	main.free()
	var save_err := ResourceSaver.save(main_scene, "res://scenes/meridian_main.tscn")
	print("Saved res://scenes/meridian_main.tscn: pack_err=", pack_err, " save_err=", save_err)
	quit(0 if (pack_err == OK and save_err == OK and val_err == OK) else 1)
