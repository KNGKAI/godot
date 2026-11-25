class_name HydratedEmergencyVehicle
extends Node3D

## Pooled Tier 2 Node3D controller for Blackout Protocol's Fire Trucks and Evacuation Sedans.

var hydratable: HydratableActor
var siren_light: OmniLight3D


func _ready() -> void:
	hydratable = get_node_or_null("Hydratable") as HydratableActor
	siren_light = get_node_or_null("SirenLight") as OmniLight3D
	if hydratable:
		hydratable.hydrated.connect(_on_hydrated)
		hydratable.capturing_state.connect(_on_capturing_state)


func _physics_process(delta: float) -> void:
	if not hydratable or not hydratable.bound_rid.is_valid():
		return
	var vel: Vector3 = SimulationServer.get_agent_velocity(hydratable.bound_rid)
	global_position += vel * delta


func _on_hydrated(entity_rid: RID, initial_state: Dictionary) -> void:
	var health: float = initial_state.get("health", 100.0)
	if siren_light:
		var arch: StringName = SimulationServer.get_agent_archetype(entity_rid)
		siren_light.light_color = Color(1.0, 0.18, 0.12) if (arch == &"fire_truck" and health > 0.0) else Color(1.0, 0.85, 0.35)


func _on_capturing_state(entity_rid: RID, out_state: Dictionary) -> void:
	out_state["health"] = SimulationServer.get_agent_health(entity_rid)
