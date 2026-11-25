class_name OrbitalRtsController
extends Node3D

## Top-Down Orbital RTS Command Rig (Domain B).
## Uses an orthographic Camera3D and responds to the exact same SandboxActor signals
## (&"camera.cutscene_zoom", &"camera.reset_view", control_lock_requested, teleport_requested)
## as the 1st-person CommanderRigController (proving Gate 3 Domain Agnosticism).

@export var pan_speed: float = 45.0
@export var min_ortho_size: float = 12.0
@export var max_ortho_size: float = 140.0

var camera: Camera3D
var actor: SandboxActor
var invoker: StreamingInvoker


func _ready() -> void:
	camera = $Camera3D as Camera3D
	actor = $SandboxActor as SandboxActor
	invoker = $StreamingInvoker as StreamingInvoker
	actor.control_lock_requested.connect(_on_control_lock_requested)
	actor.teleport_requested.connect(_on_teleport_requested)
	actor.action_requested.connect(_on_action_requested)


func _unhandled_input(event: InputEvent) -> void:
	if not camera or not camera.current or (actor and actor.is_control_locked()):
		return
	if event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			camera.size = maxf(min_ortho_size, camera.size - 4.0)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			camera.size = minf(max_ortho_size, camera.size + 4.0)


func _physics_process(delta: float) -> void:
	if not camera or not camera.current or (actor and actor.is_control_locked()):
		return
	var pan := Vector3.ZERO
	if Input.is_key_pressed(KEY_W) or Input.is_key_pressed(KEY_UP):
		pan.z -= 1.0
	if Input.is_key_pressed(KEY_S) or Input.is_key_pressed(KEY_DOWN):
		pan.z += 1.0
	if Input.is_key_pressed(KEY_A) or Input.is_key_pressed(KEY_LEFT):
		pan.x -= 1.0
	if Input.is_key_pressed(KEY_D) or Input.is_key_pressed(KEY_RIGHT):
		pan.x += 1.0
	if not pan.is_zero_approx():
		global_position += pan.normalized() * pan_speed * delta


func _on_control_lock_requested(_locked: bool, _reason: String) -> void:
	pass


func _on_teleport_requested(target_transform: Transform3D) -> void:
	global_position = Vector3(target_transform.origin.x, global_position.y, target_transform.origin.z)
	reset_physics_interpolation()


func _on_action_requested(action_id: StringName, payload: Dictionary) -> void:
	if action_id == &"camera.cutscene_zoom" and camera:
		var target_size: float = float(payload.get("ortho_size", 20.0))
		camera.size = target_size
	elif action_id == &"camera.reset_view" and camera:
		camera.size = 42.0
