extends SceneTree

var sampled_health := 91.25
var restored_health := 0.0


func _initialize() -> void:
	run.call_deferred()


func run() -> void:
	var world := Node3D.new()
	root.add_child(world)
	world.add_child(StreamingInvoker.new())
	var prefab := Node3D.new()
	var component := HydratableActor.new()
	component.name = "Hydratable"
	prefab.add_child(component)
	component.owner = prefab
	var scene := PackedScene.new()
	var err := scene.pack(prefab)
	prefab.free()
	if err != OK or HydrationBubble.configure_archetype(&"patrol", scene, 1) != OK or HydrationBubble.start(world) != OK:
		push_error("Could not configure the rewind probe")
		quit(1)
		return
	SandboxClock.set_sim_paused(true)
	var rid := SimulationServer.create_agent(&"patrol")
	for frame in 10:
		await process_frame
		if HydrationBubble.get_active_actor(rid):
			break
	var actor := HydrationBubble.get_active_actor(rid)
	if not actor:
		push_error("The clock did not hydrate the probe actor")
		quit(1)
		return
	var live: HydratableActor = actor.get_node("Hydratable")
	live.capturing_state.connect(func(_rid: RID, state: Dictionary): state.health = sampled_health)
	live.hydrated.connect(func(_rid: RID, state: Dictionary): restored_health = state.health)
	live.set_pinned(true, &"mission")
	actor.global_position = Vector3(3, 0, 7)
	SimulationServer.capture_rewind_snapshot_now()
	var saved_tick := SandboxClock.get_sim_tick_index()
	sampled_health = 18.0
	live.set_pinned(false, &"mission")
	actor.global_position = Vector3(21, 0, 5)
	SandboxClock.step_immediate(5)
	var result := SimulationServer.rewind(5)
	var passed := (result == OK and SandboxClock.get_sim_tick_index() == saved_tick
			and HydrationBubble.get_active_actor(rid) == actor and live.bound_rid == rid
			and actor.global_position == Vector3(3, 0, 7)
			and restored_health == 91.25 and SimulationServer.get_agent_health(rid) == restored_health
			and SimulationServer.is_agent_pinned(rid))
	# Exercise the host tree-exit path with a shell still active.
	world.free()
	passed = passed and not CommandBus.has_command(&"bubble.stats") and SimulationServer.get_agent_hydration_tier(rid) == 0
	SimulationServer.free_agent(rid)
	print("Hydrated five-tick rewind and world teardown: ", "PASS" if passed else "FAIL")
	quit(0 if passed else 1)
