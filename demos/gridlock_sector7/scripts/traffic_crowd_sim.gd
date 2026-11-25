class_name TrafficCrowdSim
extends SimModule

## Sector 7 Urban Traffic & Crowd Simulation Module.
## Manages looped corridor traffic velocity and signal state in WorldState.

@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:bool") var signal_override_key: StringName = &"traffic.signals_green"
@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:float") var avg_speed_key: StringName = &"traffic.avg_speed_mps"


func _init() -> void:
	module_id = &"traffic_crowd_sim"
	tick_rate_hz = 5.0
	priority = 1
	thread_safe_tick = false


func _get_declared_schema() -> Dictionary:
	return {
		"state_keys": [
			{
				"key": "traffic.signals_green",
				"value_type": TYPE_BOOL,
				"default_value": false,
				"description": "True when Sector 7 intersection signals are synchronized green."
			},
			{
				"key": "traffic.avg_speed_mps",
				"value_type": TYPE_FLOAT,
				"default_value": 12.0,
				"range_hint": Vector2(0.0, 40.0),
				"description": "Target traffic flow speed along the Sector 7 artery in m/s."
			},
			{
				"key": "traffic.active_agents",
				"value_type": TYPE_INT,
				"default_value": 0,
				"description": "Total simulated vehicles and pedestrians in Sector 7."
			}
		],
		"events": [
			{
				"event_id": "traffic.corridor_cleared",
				"description": "Emitted when emergency traffic signals are overridden to green."
			}
		]
	}


func _on_init(_state: Object, _events: Object) -> void:
	if not WorldState.has_key(&"traffic.signals_green"):
		WorldState.register_bool_channel(&"traffic.signals_green", false)
	if not WorldState.has_key(&"traffic.avg_speed_mps"):
		WorldState.register_float_channel(&"traffic.avg_speed_mps", 12.0)
	if not WorldState.has_key(&"traffic.active_agents"):
		WorldState.register_int_channel(&"traffic.active_agents", 0)


func _on_tick(_delta_sim: float, _state: Object, _events: Object) -> void:
	var green: bool = WorldState.get_bool(&"traffic.signals_green", false) or WorldState.get_bool(&"power.grid_stabilized", false)
	var target_speed: float = 20.0 if green else 6.0
	WorldState.set_float(&"traffic.avg_speed_mps", target_speed)
	WorldState.set_int(&"traffic.active_agents", SimulationServer.get_agent_count())

	# Wrap Tier 0/1/2 agents along the 800m district boulevard [-400, 400]
	var count: int = SimulationServer.get_agent_count()
	for i in count:
		var rid: RID = SimulationServer.get_agent_rid_at(i)
		if not rid.is_valid() or SimulationServer.is_agent_pinned(rid):
			continue
		var pos: Vector3 = SimulationServer.get_agent_local_position(rid)
		var arch: StringName = SimulationServer.get_agent_archetype(rid)
		var dir_sign: float = 1.0 if (i % 2 == 0) else -1.0
		var speed: float = target_speed if arch == &"cyber_cruiser" else 1.8
		SimulationServer.set_agent_velocity(rid, Vector3(0.0, 0.0, dir_sign * speed))
		if pos.z > 380.0 and dir_sign > 0.0:
			var origin := OriginShifter.get_origin_64()
			SimulationServer.set_agent_position_64(rid, origin[0] + pos.x, origin[1] + pos.y, origin[2] - 380.0)
		elif pos.z < -380.0 and dir_sign < 0.0:
			var origin := OriginShifter.get_origin_64()
			SimulationServer.set_agent_position_64(rid, origin[0] + pos.x, origin[1] + pos.y, origin[2] + 380.0)
