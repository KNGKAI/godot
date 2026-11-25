class_name EvacuationTrafficSim
extends SimModule

## Blackout Protocol Mass Evacuation & Emergency Convoy Simulation Module (Priority 2).
## Simulates 400 civilian evacuation vehicles and emergency fire trucks along the coastal highway.

@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:bool") var contraflow_key: StringName = &"evac.contraflow_active"
@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:float") var evacuated_pct_key: StringName = &"evac.civilians_evacuated_pct"
@export_custom(PROPERTY_HINT_NONE, "sandbox_event_id") var evac_complete_event: StringName = &"evac.corridor_secured"

var _evac_fired: bool = false


func _init() -> void:
	module_id = &"evacuation_traffic_sim"
	tick_rate_hz = 5.0
	priority = 2
	thread_safe_tick = false


func _get_declared_schema() -> Dictionary:
	return {
		"state_keys": [
			{
				"key": "evac.contraflow_active",
				"value_type": TYPE_BOOL,
				"default_value": false,
				"description": "True when all coastal highway lanes are reversed for emergency evacuation contraflow."
			},
			{
				"key": "evac.civilians_evacuated_pct",
				"value_type": TYPE_FLOAT,
				"default_value": 22.0,
				"range_hint": Vector2(0.0, 100.0),
				"description": "Percentage of coastal district residents evacuated to high ground (target >= 90%)."
			},
			{
				"key": "evac.active_vehicles",
				"value_type": TYPE_INT,
				"default_value": 400,
				"description": "Total active civilian cars and fire trucks in SimulationServer SoA memory."
			}
		],
		"events": [
			{
				"event_id": "evac.radar_swept",
				"description": "Emitted when Rescue-1 links its airborne Doppler & thermal radar."
			},
			{
				"event_id": "evac.contraflow_enabled",
				"description": "Emitted when highway contraflow lanes are opened."
			},
			{
				"event_id": "evac.corridor_secured",
				"description": "Emitted when civilian evacuation reaches >= 90%."
			}
		]
	}


func _on_init(_state: Object, _events: Object) -> void:
	if not WorldState.has_key(&"evac.contraflow_active"):
		WorldState.register_bool_channel(&"evac.contraflow_active", false)
	if not WorldState.has_key(&"evac.civilians_evacuated_pct"):
		WorldState.register_float_channel(&"evac.civilians_evacuated_pct", 22.0)
	if not WorldState.has_key(&"evac.active_vehicles"):
		WorldState.register_int_channel(&"evac.active_vehicles", 400)


func _on_tick(delta_sim: float, _state: Object, _events: Object) -> void:
	var contraflow: bool = WorldState.get_bool(&"evac.contraflow_active", false)
	var trucks: bool = WorldState.get_bool(&"fire.trucks_dispatched", false)
	var gates: bool = WorldState.get_bool(&"weather.seawall_gates_open", false)
	var evac_pct: float = WorldState.get_float(&"evac.civilians_evacuated_pct", 22.0)

	var evac_rate: float = 3.5
	if contraflow:
		evac_rate += 22.0
	if trucks:
		evac_rate += 10.0
	if gates:
		evac_rate += 12.0

	evac_pct = minf(100.0, evac_pct + evac_rate * delta_sim)
	WorldState.set_float(&"evac.civilians_evacuated_pct", evac_pct)

	var count: int = SimulationServer.get_agent_count()
	WorldState.set_int(&"evac.active_vehicles", count)
	var speed: float = 28.0 if contraflow else 11.0

	for i in count:
		var rid: RID = SimulationServer.get_agent_rid_at(i)
		if not rid.is_valid() or SimulationServer.is_agent_pinned(rid):
			continue
		var arch: StringName = SimulationServer.get_agent_archetype(rid)
		var v_speed: float = (speed * 1.25) if arch == &"fire_truck" else speed
		SimulationServer.set_agent_velocity(rid, Vector3(v_speed, 0.0, 0.0))

	if evac_pct >= 90.0 and not _evac_fired:
		_evac_fired = true
		EventBus.emit(&"evac.corridor_secured", {"evacuated_pct": evac_pct})


func _on_snapshot_write() -> PackedByteArray:
	var buf := PackedByteArray()
	buf.resize(1)
	buf[0] = 1 if _evac_fired else 0
	return buf


func _on_snapshot_read(data: PackedByteArray) -> void:
	if data.size() >= 1:
		_evac_fired = data[0] != 0
