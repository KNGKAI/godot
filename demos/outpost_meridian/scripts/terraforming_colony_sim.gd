class_name TerraformingColonySim
extends SimModule

## Outpost Meridian Atmospheric & Solar Colony Simulation Module.
## Computes solar array output, battery reserve, O2 pressure, dust storm drains, and ore processing.

@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:float") var o2_key: StringName = &"colony.o2_pressure_kpa"
@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:float") var battery_key: StringName = &"colony.battery_mwh"
@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:float") var ore_key: StringName = &"colony.refined_ore_tons"
@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:bool") var scrubber_key: StringName = &"colony.scrubber_overdrive"
@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:bool") var storm_shield_key: StringName = &"colony.storm_shield_active"
@export_custom(PROPERTY_HINT_NONE, "sandbox_event_id") var quota_event: StringName = &"colony.quota_reached"

var _quota_fired: bool = false


func _init() -> void:
	module_id = &"terraforming_colony_sim"
	tick_rate_hz = 10.0
	priority = 0
	thread_safe_tick = false


func _get_declared_schema() -> Dictionary:
	return {
		"state_keys": [
			{
				"key": "colony.o2_pressure_kpa",
				"value_type": TYPE_FLOAT,
				"default_value": 68.0,
				"range_hint": Vector2(0.0, 120.0),
				"description": "Habitat atmospheric O2 partial pressure in kPa (nominal >= 95 kPa)."
			},
			{
				"key": "colony.battery_mwh",
				"value_type": TYPE_FLOAT,
				"default_value": 140.0,
				"range_hint": Vector2(0.0, 500.0),
				"description": "Colony solar capacitor reserve in MWh."
			},
			{
				"key": "colony.refined_ore_tons",
				"value_type": TYPE_FLOAT,
				"default_value": 45.0,
				"range_hint": Vector2(0.0, 1000.0),
				"description": "Processed regolith titanium/ice ore in metric tons (target quota >= 250 tons)."
			},
			{
				"key": "colony.scrubber_overdrive",
				"value_type": TYPE_BOOL,
				"default_value": false,
				"description": "True when atmospheric O2 scrubbers are running in high-output overdrive."
			},
			{
				"key": "colony.storm_shield_active",
				"value_type": TYPE_BOOL,
				"default_value": false,
				"description": "True when electromagnetic dust shields are protecting the crater array."
			},
			{
				"key": "colony.dome_beacon_energy",
				"value_type": TYPE_FLOAT,
				"default_value": 0.5,
				"range_hint": Vector2(0.0, 3.0),
				"description": "Habitat dome telemetry beacon brightness."
			}
		],
		"events": [
			{
				"event_id": "colony.telemetry_linked",
				"description": "Emitted when orbital telemetry is synchronized at the Command Deck."
			},
			{
				"event_id": "colony.scrubber_boosted",
				"description": "Emitted when atmospheric scrubbers enter overdrive."
			},
			{
				"event_id": "colony.convoy_recalled",
				"description": "Emitted when the deep-crater hauler fleet is boosted/recalled."
			},
			{
				"event_id": "colony.quota_reached",
				"description": "Emitted when colony O2 >= 95 kPa and refined ore >= 250 tons."
			}
		]
	}


func _on_init(_state: Object, _events: Object) -> void:
	if not WorldState.has_key(&"colony.o2_pressure_kpa"):
		WorldState.register_float_channel(&"colony.o2_pressure_kpa", 68.0)
	if not WorldState.has_key(&"colony.battery_mwh"):
		WorldState.register_float_channel(&"colony.battery_mwh", 140.0)
	if not WorldState.has_key(&"colony.refined_ore_tons"):
		WorldState.register_float_channel(&"colony.refined_ore_tons", 45.0)
	if not WorldState.has_key(&"colony.scrubber_overdrive"):
		WorldState.register_bool_channel(&"colony.scrubber_overdrive", false)
	if not WorldState.has_key(&"colony.storm_shield_active"):
		WorldState.register_bool_channel(&"colony.storm_shield_active", false)
	if not WorldState.has_key(&"colony.dome_beacon_energy"):
		WorldState.register_float_channel(&"colony.dome_beacon_energy", 0.5)


func _on_tick(delta_sim: float, _state: Object, _events: Object) -> void:
	var o2: float = WorldState.get_float(&"colony.o2_pressure_kpa", 68.0)
	var battery: float = WorldState.get_float(&"colony.battery_mwh", 140.0)
	var ore: float = WorldState.get_float(&"colony.refined_ore_tons", 45.0)
	var overdrive: bool = WorldState.get_bool(&"colony.scrubber_overdrive", false)
	var convoy_boost: bool = WorldState.get_bool(&"fleet.convoy_overclock", false)

	var o2_rate: float = 4.2 if overdrive else 0.35
	o2 = minf(105.0, o2 + o2_rate * delta_sim)

	var ore_rate: float = 28.0 if convoy_boost else 6.5
	if overdrive:
		ore_rate += 12.0
	ore = minf(600.0, ore + ore_rate * delta_sim)

	var net_power: float = 8.0 - (3.5 if overdrive else 0.5) - (2.5 if convoy_boost else 0.0)
	battery = clampf(battery + net_power * delta_sim, 10.0, 500.0)

	WorldState.set_float(&"colony.o2_pressure_kpa", o2)
	WorldState.set_float(&"colony.battery_mwh", battery)
	WorldState.set_float(&"colony.refined_ore_tons", ore)

	if o2 >= 95.0 and ore >= 250.0:
		WorldState.set_float(&"colony.dome_beacon_energy", 2.2)
		if not _quota_fired:
			_quota_fired = true
			EventBus.emit(&"colony.quota_reached", {"o2_kpa": o2, "ore_tons": ore})


func _on_snapshot_write() -> PackedByteArray:
	var buf := PackedByteArray()
	buf.resize(1)
	buf[0] = 1 if _quota_fired else 0
	return buf


func _on_snapshot_read(data: PackedByteArray) -> void:
	if data.size() >= 1:
		_quota_fired = data[0] != 0
