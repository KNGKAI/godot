class_name PowerGridSim
extends SimModule

## Sector 7 Substation & Transformer Simulation Module.
## Computes thermal load, cooling valve effects, brownouts, and corporate sector venting.

@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:float") var load_key: StringName = &"power.sector7_load_mw"
@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:float") var temp_key: StringName = &"power.transformer_temp_c"
@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:bool") var coolant_key: StringName = &"power.coolant_valve_open"
@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:bool") var vented_key: StringName = &"power.corp_sector_vented"
@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:bool") var stabilized_key: StringName = &"power.grid_stabilized"
@export_custom(PROPERTY_HINT_NONE, "sandbox_event_id") var stabilized_event: StringName = &"grid.stabilized"
@export_custom(PROPERTY_HINT_NONE, "sandbox_event_id") var alarm_event: StringName = &"grid.overload_alarm"

var _alarm_fired: bool = false
var _stabilized_fired: bool = false


func _init() -> void:
	module_id = &"power_grid_sim"
	tick_rate_hz = 10.0
	priority = 0
	thread_safe_tick = false


func _get_declared_schema() -> Dictionary:
	return {
		"state_keys": [
			{
				"key": "power.sector7_load_mw",
				"value_type": TYPE_FLOAT,
				"default_value": 94.0,
				"range_hint": Vector2(0.0, 150.0),
				"description": "Current load in Megawatts on Sector 7 primary substation."
			},
			{
				"key": "power.transformer_temp_c",
				"value_type": TYPE_FLOAT,
				"default_value": 112.0,
				"range_hint": Vector2(20.0, 250.0),
				"description": "Core transformer temperature in degrees Celsius."
			},
			{
				"key": "power.coolant_valve_open",
				"value_type": TYPE_BOOL,
				"default_value": false,
				"description": "True when the auxiliary cryo-coolant valve is open."
			},
			{
				"key": "power.corp_sector_vented",
				"value_type": TYPE_BOOL,
				"default_value": false,
				"description": "True when overload current has been shunted into the corporate plaza."
			},
			{
				"key": "power.grid_stabilized",
				"value_type": TYPE_BOOL,
				"default_value": false,
				"description": "True once Sector 7 load drops below 75 MW and temp drops below 85 C."
			},
			{
				"key": "power.streetlights_dimmer",
				"value_type": TYPE_FLOAT,
				"default_value": 0.35,
				"range_hint": Vector2(0.0, 1.5),
				"description": "District streetlight and neon brightness multiplier."
			}
		],
		"events": [
			{
				"event_id": "grid.kiosk_diagnostic_run",
				"description": "Emitted when the field technician runs a diagnostic scan at the street kiosk."
			},
			{
				"event_id": "grid.coolant_opened",
				"description": "Emitted when the physical coolant valve or power.coolant command opens cooling."
			},
			{
				"event_id": "grid.corp_surge_vented",
				"description": "Emitted when excess substation load is shunted into the corporate sector."
			},
			{
				"event_id": "grid.overload_alarm",
				"description": "Emitted when transformer temperature crosses 120 C."
			},
			{
				"event_id": "grid.stabilized",
				"description": "Emitted when Sector 7 power grid returns to nominal operating limits."
			}
		]
	}


func _on_init(_state: Object, _events: Object) -> void:
	if not WorldState.has_key(&"power.sector7_load_mw"):
		WorldState.register_float_channel(&"power.sector7_load_mw", 94.0)
	if not WorldState.has_key(&"power.transformer_temp_c"):
		WorldState.register_float_channel(&"power.transformer_temp_c", 112.0)
	if not WorldState.has_key(&"power.coolant_valve_open"):
		WorldState.register_bool_channel(&"power.coolant_valve_open", false)
	if not WorldState.has_key(&"power.corp_sector_vented"):
		WorldState.register_bool_channel(&"power.corp_sector_vented", false)
	if not WorldState.has_key(&"power.grid_stabilized"):
		WorldState.register_bool_channel(&"power.grid_stabilized", false)
	if not WorldState.has_key(&"power.streetlights_dimmer"):
		WorldState.register_float_channel(&"power.streetlights_dimmer", 0.35)


func _on_tick(delta_sim: float, _state: Object, _events: Object) -> void:
	var load_mw: float = WorldState.get_float(&"power.sector7_load_mw", 94.0)
	var temp_c: float = WorldState.get_float(&"power.transformer_temp_c", 112.0)
	var coolant_open: bool = WorldState.get_bool(&"power.coolant_valve_open", false)
	var corp_vented: bool = WorldState.get_bool(&"power.corp_sector_vented", false)

	var target_load: float = 96.0
	if corp_vented:
		target_load = 52.0
	elif coolant_open:
		target_load = 64.0

	load_mw = move_toward(load_mw, target_load, delta_sim * 18.0)

	var target_temp: float = 45.0 + load_mw * 0.78
	if coolant_open:
		target_temp -= 32.0
	if corp_vented:
		target_temp -= 18.0

	temp_c = move_toward(temp_c, target_temp, delta_sim * 22.0)

	WorldState.set_float(&"power.sector7_load_mw", load_mw)
	WorldState.set_float(&"power.transformer_temp_c", temp_c)

	if temp_c >= 115.0 and not _alarm_fired:
		_alarm_fired = true
		EventBus.emit(&"grid.overload_alarm", {"temp_c": temp_c, "load_mw": load_mw})

	var is_stable: bool = (load_mw <= 72.0 and temp_c <= 82.0) or coolant_open or corp_vented
	if is_stable and load_mw <= 72.0 and temp_c <= 82.0:
		if not WorldState.get_bool(&"power.grid_stabilized", false):
			WorldState.set_bool(&"power.grid_stabilized", true)
			WorldState.set_float(&"power.streetlights_dimmer", 1.15)
		if not _stabilized_fired:
			_stabilized_fired = true
			var route: String = "corp_vent" if corp_vented else "coolant_repair"
			EventBus.emit(&"grid.stabilized", {"route": route, "load_mw": load_mw, "temp_c": temp_c})


func _on_snapshot_write() -> PackedByteArray:
	var buf := PackedByteArray()
	buf.resize(2)
	buf[0] = 1 if _alarm_fired else 0
	buf[1] = 1 if _stabilized_fired else 0
	return buf


func _on_snapshot_read(data: PackedByteArray) -> void:
	if data.size() >= 2:
		_alarm_fired = data[0] != 0
		_stabilized_fired = data[1] != 0
