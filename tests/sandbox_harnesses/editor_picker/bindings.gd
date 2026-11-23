@tool
extends Node3D

@export_custom(PROPERTY_HINT_NONE, "sandbox_marker_id") var waypoint: StringName = &"MissingMarker"
@export_custom(PROPERTY_HINT_NONE, "sandbox_actor_id") var agent: StringName = &"missing.actor"
@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:float") var state_key: StringName = &"district.power.locked"
