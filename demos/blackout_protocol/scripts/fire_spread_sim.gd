class_name FireSpreadSim
extends SimModule

## Blackout Protocol Cellular Urban Fire Propagation & Suppression Module (Priority 1).
## Reads wind speed & rain intensity from WorldState, computes industrial refinery & dock fire intensity,
## and processes aerial water-drop / ground fire-truck foam suppression.

@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:float") var fire_intensity_key: StringName = &"fire.city_hazard_pct"
@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:int") var burning_blocks_key: StringName = &"fire.burning_blocks"
@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:bool") var trucks_dispatched_key: StringName = &"fire.trucks_dispatched"
@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:bool") var airdrop_deployed_key: StringName = &"fire.airdrop_deployed"
@export_custom(PROPERTY_HINT_NONE, "sandbox_event_id") var contained_event: StringName = &"fire.all_contained"

var _contained_fired: bool = false


func _init() -> void:
	module_id = &"fire_spread_sim"
	tick_rate_hz = 10.0
	priority = 1
	thread_safe_tick = false


func _get_declared_schema() -> Dictionary:
	return {
		"state_keys": [
			{
				"key": "fire.city_hazard_pct",
				"value_type": TYPE_FLOAT,
				"default_value": 78.0,
				"range_hint": Vector2(0.0, 100.0),
				"description": "Aggregate urban fire hazard percentage across coastal blocks (target <= 15%)."
			},
			{
				"key": "fire.burning_blocks",
				"value_type": TYPE_INT,
				"default_value": 6,
				"range_hint": Vector2(0.0, 16.0),
				"description": "Count of active burning city blocks."
			},
			{
				"key": "fire.trucks_dispatched",
				"value_type": TYPE_BOOL,
				"default_value": false,
				"description": "True when ground foam-suppression fire trucks are deployed."
			},
			{
				"key": "fire.airdrop_deployed",
				"value_type": TYPE_BOOL,
				"default_value": false,
				"description": "True when helicopter water/retardant drop has been executed over the refinery."
			}
		],
		"events": [
			{
				"event_id": "fire.trucks_deployed",
				"description": "Emitted when ground fire brigades are dispatched."
			},
			{
				"event_id": "fire.airdrop_executed",
				"description": "Emitted when Rescue-1 executes an aerial retardant drop."
			},
			{
				"event_id": "fire.all_contained",
				"description": "Emitted when city fire hazard drops to <= 15%."
			}
		]
	}


func _on_init(_state: Object, _events: Object) -> void:
	if not WorldState.has_key(&"fire.city_hazard_pct"):
		WorldState.register_float_channel(&"fire.city_hazard_pct", 78.0)
	if not WorldState.has_key(&"fire.burning_blocks"):
		WorldState.register_int_channel(&"fire.burning_blocks", 6)
	if not WorldState.has_key(&"fire.trucks_dispatched"):
		WorldState.register_bool_channel(&"fire.trucks_dispatched", false)
	if not WorldState.has_key(&"fire.airdrop_deployed"):
		WorldState.register_bool_channel(&"fire.airdrop_deployed", false)


func _on_tick(delta_sim: float, _state: Object, _events: Object) -> void:
	var hazard: float = WorldState.get_float(&"fire.city_hazard_pct", 78.0)
	var wind_knots: float = WorldState.get_float(&"weather.wind_speed_knots", 58.0)
	var rain: float = WorldState.get_float(&"weather.rain_intensity", 0.45)
	var trucks: bool = WorldState.get_bool(&"fire.trucks_dispatched", false)
	var airdrop: bool = WorldState.get_bool(&"fire.airdrop_deployed", false)

	var growth_rate: float = (wind_knots * 0.06) - (rain * 2.2)
	var suppression_rate: float = (18.0 if trucks else 0.0) + (26.0 if airdrop else 0.0)
	var net_rate: float = growth_rate - suppression_rate

	hazard = clampf(hazard + net_rate * delta_sim, 0.0, 100.0)
	var blocks: int = int(ceil(hazard / 16.0)) if hazard > 12.0 else 0

	WorldState.set_float(&"fire.city_hazard_pct", hazard)
	WorldState.set_int(&"fire.burning_blocks", blocks)

	if hazard <= 15.0 and not _contained_fired:
		_contained_fired = true
		EventBus.emit(&"fire.all_contained", {"hazard_pct": hazard})


func _on_snapshot_write() -> PackedByteArray:
	var buf := PackedByteArray()
	buf.resize(1)
	buf[0] = 1 if _contained_fired else 0
	return buf


func _on_snapshot_read(data: PackedByteArray) -> void:
	if data.size() >= 1:
		_contained_fired = data[0] != 0
