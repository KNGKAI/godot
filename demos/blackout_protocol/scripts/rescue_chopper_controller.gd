class_name RescueChopperController
extends CharacterBody3D

## High-Speed Rescue-1 Helicopter Rig (90 m/s Cruise, 3.5s StreamingInvoker Lookahead).
## Includes an in-cockpit diegetic 3D terminal (ChopperCockpitTerminal) and responds to
## SandboxActor signals (control_lock_requested, teleport_requested, action_requested).

@export var cruise_speed: float = 38.0
@export var turbo_speed: float = 95.0
@export var vertical_speed: float = 22.0
@export var mouse_sensitivity: float = 0.0025

var camera: Camera3D
var actor: SandboxActor
var invoker: StreamingInvoker
var rotor_mesh: Node3D
var pitch: float = -0.15


func _ready() -> void:
	camera = $Camera3D as Camera3D
	actor = $SandboxActor as SandboxActor
	invoker = $StreamingInvoker as StreamingInvoker
	rotor_mesh = get_node_or_null("RotorBlades") as Node3D

	actor.control_lock_requested.connect(_on_control_lock_requested)
	actor.teleport_requested.connect(_on_teleport_requested)
	actor.action_requested.connect(_on_action_requested)


func _unhandled_input(event: InputEvent) -> void:
	if actor and actor.is_control_locked():
		return
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_RIGHT:
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if event.pressed else Input.MOUSE_MODE_VISIBLE
	elif event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		rotate_y(-event.relative.x * mouse_sensitivity)
		pitch = clampf(pitch - event.relative.y * mouse_sensitivity, -1.25, 1.1)
		if camera:
			camera.rotation.x = pitch


func _physics_process(delta: float) -> void:
	if rotor_mesh:
		rotor_mesh.rotate_y(delta * 24.0)
	if actor and actor.is_control_locked():
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

	var climb: float = 0.0
	if Input.is_key_pressed(KEY_E) or Input.is_key_pressed(KEY_SPACE):
		climb += 1.0
	if Input.is_key_pressed(KEY_Q) or Input.is_key_pressed(KEY_CTRL):
		climb -= 1.0

	var speed: float = turbo_speed if Input.is_key_pressed(KEY_SHIFT) else cruise_speed
	var wish_dir: Vector3 = (global_transform.basis * input_dir).normalized()
	velocity.x = wish_dir.x * speed
	velocity.z = wish_dir.z * speed
	velocity.y = climb * vertical_speed
	move_and_slide()


func _on_control_lock_requested(locked: bool, _reason: String) -> void:
	if locked:
		velocity = Vector3.ZERO


func _on_teleport_requested(target_transform: Transform3D) -> void:
	global_transform = target_transform
	reset_physics_interpolation()


func _on_action_requested(action_id: StringName, payload: Dictionary) -> void:
	if action_id == &"chopper.flir_zoom" and camera:
		camera.fov = float(payload.get("fov", 46.0))
	elif action_id == &"chopper.reset_fov" and camera:
		camera.fov = 75.0
