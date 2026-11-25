class_name WeatherStormSim
extends SimModule

## Blackout Protocol Coastal Storm & Flood Surge Simulation Module (Priority 0).
## Computes wind speed, rainfall intensity, storm surge water level, and seawall floodgate relief.

@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:float") var wind_key: StringName = &"weather.wind_speed_knots"
@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:float") var rain_key: StringName = &"weather.rain_intensity"
@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:float") var flood_key: StringName = &"weather.flood_level_m"
@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:bool") var floodgate_key: StringName = &"weather.seawall_gates_open"
@export_custom(PROPERTY_HINT_NONE, "sandbox_event_id") var storm_peak_event: StringName = &"weather.storm_surge_warning"

var _warning_fired: bool = false


func _init() -> void:
	module_id = &"weather_storm_sim"
	tick_rate_hz = 10.0
	priority = 0
	thread_safe_tick = false


func _get_declared_schema() -> Dictionary:
	return {
		"state_keys": [
			{
				"key": "weather.wind_speed_knots",
				"value_type": TYPE_FLOAT,
				"default_value": 58.0,
				"range_hint": Vector2(0.0, 120.0),
				"description": "Coastal gale wind speed in knots (accelerates fire spread)."
			},
			{
				"key": "weather.rain_intensity",
				"value_type": TYPE_FLOAT,
				"default_value": 0.45,
				"range_hint": Vector2(0.0, 1.0),
				"description": "Normalized rainfall intensity [0.0, 1.0]."
			},
			{
				"key": "weather.flood_level_m",
				"value_type": TYPE_FLOAT,
				"default_value": 3.4,
				"range_hint": Vector2(0.0, 10.0),
				"description": "Harbor storm surge water elevation in meters."
			},
			{
				"key": "weather.seawall_gates_open",
				"value_type": TYPE_BOOL,
				"default_value": false,
				"description": "True when harbor diversion floodgates are open to drain storm surge."
			}
		],
		"events": [
			{
				"event_id": "weather.storm_surge_warning",
				"description": "Emitted when harbor storm surge exceeds 3.5 meters."
			},
			{
				"event_id": "weather.floodgates_opened",
				"description": "Emitted when coastal floodgates are opened to drain harbor surge."
			}
		]
	}


func _on_init(_state: Object, _events: Object) -> void:
	if not WorldState.has_key(&"weather.wind_speed_knots"):
		WorldState.register_float_channel(&"weather.wind_speed_knots", 58.0)
	if not WorldState.has_key(&"weather.rain_intensity"):
		WorldState.register_float_channel(&"weather.rain_intensity", 0.45)
	if not WorldState.has_key(&"weather.flood_level_m"):
		WorldState.register_float_channel(&"weather.flood_level_m", 3.4)
	if not WorldState.has_key(&"weather.seawall_gates_open"):
		WorldState.register_bool_channel(&"weather.seawall_gates_open", false)


func _on_tick(delta_sim: float, _state: Object, _events: Object) -> void:
	var flood: float = WorldState.get_float(&"weather.flood_level_m", 3.4)
	var gates_open: bool = WorldState.get_bool(&"weather.seawall_gates_open", false)
	var target_flood: float = 1.2 if gates_open else 4.2
	flood = move_toward(flood, target_flood, delta_sim * 0.85)
	WorldState.set_float(&"weather.flood_level_m", flood)

	var wind: float = 34.0 if gates_open else 58.0
	WorldState.set_float(&"weather.wind_speed_knots", move_toward(WorldState.get_float(&"weather.wind_speed_knots", 58.0), wind, delta_sim * 6.0))

	if flood >= 3.5 and not _warning_fired:
		_warning_fired = true
		EventBus.emit(&"weather.storm_surge_warning", {"flood_level_m": flood})


func _on_snapshot_write() -> PackedByteArray:
	var buf := PackedByteArray()
	buf.resize(1)
	buf[0] = 1 if _warning_fired else 0
	return buf


func _on_snapshot_read(data: PackedByteArray) -> void:
	if data.size() >= 1:
		_warning_fired = data[0] != 0
