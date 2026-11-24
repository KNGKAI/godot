class_name HydratedHaulerController
extends Node3D

## Pooled Tier 2 Node3D controller for Outpost Meridian's Ore Haulers and Survey Drones.

var hydratable: HydratableActor
var beacon: OmniLight3D


func _ready() -> void:
	hydratable = get_node_or_null("Hydratable") as HydratableActor
	beacon = get_node_or_null("StatusLight") as OmniLight3D
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
	if beacon:
		beacon.light_color = Color(1.0, 0.78, 0.2) if health >= 50.0 else Color(1.0, 0.2, 0.1)


func _on_capturing_state(entity_rid: RID, out_state: Dictionary) -> void:
	out_state["health"] = SimulationServer.get_agent_health(entity_rid)
