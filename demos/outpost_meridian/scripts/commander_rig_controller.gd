class_name CommanderRigController
extends CharacterBody3D

## First-Person / High-Speed Rover Commander Rig (Domain A).
## Attach SandboxActor (&"commander_fp") and StreamingInvoker.
## Supports walking inside the Habitat or driving the High-Speed Crater Rover at 120 m/s
## across multiple 2048m OriginShifter boundaries.

@export var walk_speed: float = 9.0
@export var rover_speed: float = 120.0
@export var mouse_sensitivity: float = 0.0025

var camera: Camera3D
var actor: SandboxActor
var invoker: StreamingInvoker
var rover_mode: bool = false
var pitch: float = 0.0


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
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_RIGHT:
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if event.pressed else Input.MOUSE_MODE_VISIBLE
	elif event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		rotate_y(-event.relative.x * mouse_sensitivity)
		pitch = clampf(pitch - event.relative.y * mouse_sensitivity, -1.35, 1.35)
		camera.rotation.x = pitch


func _physics_process(delta: float) -> void:
	if not camera or not camera.current or (actor and actor.is_control_locked()):
		return
	var input_dir := Vector3.ZERO
	if Input.is_key_pressed(KEY_W) or Input.is_key_pressed(KEY_UP):
		input_dir.z -= 1.0
	if Input.is_key_pressed(KEY_S) or Input.is_key_pressed(KEY_DOWN):
		input_dir.z += 1.0
	if Input.is_key_pressed(KEY_A) or Input.is_key_pressed(KEY_LEFT):
		input_dir.x -= 1.0
	if Input.is_key_pressed(KEY_D) or Input.is_key_pressed(KEY_RIGHT):
		input_dir.x += 1.0

	var speed: float = rover_speed if (rover_mode or Input.is_key_pressed(KEY_SHIFT)) else walk_speed
	var wish_dir: Vector3 = (global_transform.basis * input_dir).normalized()
	velocity.x = wish_dir.x * speed
	velocity.z = wish_dir.z * speed
	if not is_on_floor():
		velocity.y -= 9.8 * delta
	else:
		velocity.y = 0.0
	move_and_slide()


func _on_control_lock_requested(locked: bool, _reason: String) -> void:
	if locked:
		velocity = Vector3.ZERO


func _on_teleport_requested(target_transform: Transform3D) -> void:
	global_transform = target_transform
	reset_physics_interpolation()


func _on_action_requested(action_id: StringName, payload: Dictionary) -> void:
	if action_id == &"camera.cutscene_zoom" and camera:
		var target_fov: float = float(payload.get("fov", 48.0))
		camera.fov = target_fov
	elif action_id == &"camera.reset_view" and camera:
		camera.fov = 75.0
	elif action_id == &"rover.toggle_drive":
		rover_mode = bool(payload.get("enabled", not rover_mode))
