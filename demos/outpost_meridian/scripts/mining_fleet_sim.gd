class_name MiningFleetSim
extends SimModule

## Outpost Meridian Autonomous Hauler & Scout Drone Fleet Simulation Module.
## Drives 300 autonomous mining haulers and survey drones across the crater floor in SoA memory.

@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:bool") var overclock_key: StringName = &"fleet.convoy_overclock"
@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:float") var hauler_speed_key: StringName = &"fleet.hauler_speed_mps"


func _init() -> void:
	module_id = &"mining_fleet_sim"
	tick_rate_hz = 5.0
	priority = 1
	thread_safe_tick = false


func _get_declared_schema() -> Dictionary:
	return {
		"state_keys": [
			{
				"key": "fleet.convoy_overclock",
				"value_type": TYPE_BOOL,
				"default_value": false,
				"description": "True when crater mining haulers are running at high-speed convoy overclock."
			},
			{
				"key": "fleet.hauler_speed_mps",
				"value_type": TYPE_FLOAT,
				"default_value": 10.0,
				"range_hint": Vector2(0.0, 45.0),
				"description": "Target speed of autonomous regolith haulers in m/s."
			},
			{
				"key": "fleet.active_units",
				"value_type": TYPE_INT,
				"default_value": 0,
				"description": "Total simulated mining haulers and survey drones in the crater."
			}
		],
		"events": [
			{
				"event_id": "fleet.origin_crossed",
				"description": "Emitted when the rescue rover crosses a 2048m OriginShifter sector boundary."
			}
		]
	}


func _on_init(_state: Object, _events: Object) -> void:
	if not WorldState.has_key(&"fleet.convoy_overclock"):
		WorldState.register_bool_channel(&"fleet.convoy_overclock", false)
	if not WorldState.has_key(&"fleet.hauler_speed_mps"):
		WorldState.register_float_channel(&"fleet.hauler_speed_mps", 10.0)
	if not WorldState.has_key(&"fleet.active_units"):
		WorldState.register_int_channel(&"fleet.active_units", 0)


func _on_tick(_delta_sim: float, _state: Object, _events: Object) -> void:
	var overclock: bool = WorldState.get_bool(&"fleet.convoy_overclock", false)
	var speed: float = 24.0 if overclock else 9.0
	WorldState.set_float(&"fleet.hauler_speed_mps", speed)
	var count: int = SimulationServer.get_agent_count()
	WorldState.set_int(&"fleet.active_units", count)

	for i in count:
		var rid: RID = SimulationServer.get_agent_rid_at(i)
		if not rid.is_valid() or SimulationServer.is_agent_pinned(rid):
			continue
		var arch: StringName = SimulationServer.get_agent_archetype(rid)
		var dir_sign: float = 1.0 if (i % 2 == 0) else -1.0
		var unit_speed: float = speed if arch == &"ore_hauler" else (speed * 1.35)
		SimulationServer.set_agent_velocity(rid, Vector3(dir_sign * unit_speed, 0.0, 0.0))
