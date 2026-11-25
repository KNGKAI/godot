class_name HydratedVehicleController
extends Node3D

## Attached to Tier 2 pooled vehicle & pedestrian shells inside HydrationBubble.
## Demonstrates state round-tripping (health, headlights, damage) and HYDRATION_PIN occupancy.

@export var is_vehicle: bool = true
var hydratable: HydratableActor
var sandbox_actor: SandboxActor
var body_mesh: MeshInstance3D
var status_light: OmniLight3D


func _ready() -> void:
	hydratable = get_node_or_null("Hydratable") as HydratableActor
	sandbox_actor = get_node_or_null("SandboxActor") as SandboxActor
	body_mesh = get_node_or_null("BodyMesh") as MeshInstance3D
	status_light = get_node_or_null("Headlight") as OmniLight3D
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
	if status_light:
		status_light.light_color = Color(0.2, 0.95, 1.0) if health > 50.0 else Color(1.0, 0.3, 0.1)


func _on_capturing_state(entity_rid: RID, out_state: Dictionary) -> void:
	out_state["health"] = SimulationServer.get_agent_health(entity_rid)
