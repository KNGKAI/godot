class_name FixerPlayerController
extends CharacterBody3D

## First-person / free-cam field technician controller for Gridlock: Sector 7.
## Responds to SandboxActor signals (control_lock_requested, teleport_requested, action_requested)
## and can board/unboard the service van or interact with diegetic 3D terminals.

@export var move_speed: float = 9.0
@export var sprint_speed: float = 22.0
@export var mouse_sensitivity: float = 0.0025

var camera: Camera3D
var actor: SandboxActor
var invoker: StreamingInvoker
var anim_player: AnimationPlayer
var pitch: float = 0.0
var driving_van: Node3D = null
var pinned_agent_rid: RID


func _ready() -> void:
	camera = $Camera3D as Camera3D
	actor = $SandboxActor as SandboxActor
	invoker = $StreamingInvoker as StreamingInvoker
	anim_player = $AnimationPlayer as AnimationPlayer

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
		pitch = clampf(pitch - event.relative.y * mouse_sensitivity, -1.35, 1.35)
		if camera:
			camera.rotation.x = pitch
	elif event is InputEventKey and event.pressed and not event.echo:
		if event.keycode == KEY_F1:
			_teleport_to_marker(&"MarkerKiosk")
		elif event.keycode == KEY_F2:
			_teleport_to_marker(&"MarkerSubstation")
		elif event.keycode == KEY_F3:
			_teleport_to_marker(&"MarkerValve")
		elif event.keycode == KEY_F4:
			var next_scale: float = 10.0 if SandboxClock.get_sim_time_scale() < 5.0 else 1.0
			SandboxClock.set_sim_time_scale(next_scale)
		elif event.keycode == KEY_F5:
			SimulationServer.rewind(40)


func _physics_process(delta: float) -> void:
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

	var speed: float = sprint_speed if Input.is_key_pressed(KEY_SHIFT) else move_speed
	var wish_dir: Vector3 = (global_transform.basis * input_dir).normalized()
	velocity.x = wish_dir.x * speed
	velocity.z = wish_dir.z * speed
	if not is_on_floor():
		velocity.y -= 18.0 * delta
	else:
		velocity.y = 0.0
	move_and_slide()
	if driving_van:
		driving_van.global_position = global_position + Vector3(0.0, -0.4, 0.0)
		driving_van.rotation.y = rotation.y


func _teleport_to_marker(marker_name: StringName) -> void:
	var root := get_tree().current_scene
	if not root:
		return
	var marker := root.find_child(String(marker_name), true, false) as Node3D
	if marker:
		_on_teleport_requested(marker.global_transform)


func _on_control_lock_requested(locked: bool, _reason: String) -> void:
	if locked:
		velocity = Vector3.ZERO


func _on_teleport_requested(target_transform: Transform3D) -> void:
	global_transform = target_transform
	reset_physics_interpolation()
	if driving_van:
		driving_van.global_transform = target_transform
		driving_van.reset_physics_interpolation()


func _on_action_requested(action_id: StringName, payload: Dictionary) -> void:
	if action_id == &"camera.focus_substation" and camera:
		var fov: float = float(payload.get("fov", 52.0))
		var tween := create_tween()
		tween.tween_property(camera, "fov", fov, 0.45)
	elif action_id == &"camera.reset_fov" and camera:
		var tween := create_tween()
		tween.tween_property(camera, "fov", 75.0, 0.45)
	elif action_id == &"player.teleport_marker":
		var marker_id: StringName = payload.get("marker_id", &"")
		if marker_id != &"":
			_teleport_to_marker(marker_id)
