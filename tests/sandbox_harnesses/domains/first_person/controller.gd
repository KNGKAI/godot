extends CharacterBody3D

func _ready() -> void:
	$Actor.action_requested.connect(_on_action)

func _on_action(action_id: StringName, payload: Dictionary) -> void:
	if action_id == &"focus_beacon" and payload.get("sector") == 7:
		$Eyes.fov = 50.0
