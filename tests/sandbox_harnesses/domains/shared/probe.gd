extends Node3D

const MISSION: MissionData = preload("res://shared/mission.tres")
const COMMAND := "mission.start arrival"

@onready var actor: SandboxActor = $Rig/Actor
@onready var player: AnimationPlayer = $Rig/Actor/Player
@onready var terminal: TerminalDisplay3D = $Terminal

var failures: Array[String] = []
var locks: Array[bool] = []
var actions: Array[StringName] = []
var events: Array[StringName] = []
var finished: Array[StringName] = []

func check(value: bool, message: String) -> void:
	if not value:
		failures.append(message)

func key(code: Key, character: int = 0) -> void:
	for pressed in [true, false]:
		var event := InputEventKey.new()
		event.keycode = code
		event.unicode = character
		event.pressed = pressed
		get_viewport().push_input(event, true)

func observe_event(id: StringName, payload: Dictionary) -> void:
	if id in [&"arrival.beacon", &"arrival.debrief"]:
		check(payload.get("sector") == 7, "Incorrect event payload " + id)
		events.append(id)

func _ready() -> void:
	var camera := get_viewport().get_camera_3d()
	var first_person := $Rig is CharacterBody3D
	if first_person:
		check(camera == $Rig/Eyes and camera.projection == Camera3D.PROJECTION_PERSPECTIVE, "First-person camera")
	else:
		check(camera == $Rig and camera.projection == Camera3D.PROJECTION_ORTHOGONAL and camera.global_basis.z.dot(Vector3.UP) > 0.99, "Top-down camera")
	check(ActorRegistry.get_actor(&"observer") == actor, "Actor registration")
	actor.control_lock_requested.connect(func(locked: bool, _reason: String) -> void: locks.append(locked))
	actor.action_requested.connect(func(action: StringName, _payload: Dictionary) -> void: actions.append(action))
	player.animation_finished.connect(func(animation: StringName) -> void: finished.append(animation))
	EventBus.event_emitted.connect(observe_event)
	MissionRunner.register_mission(MISSION)
	for i in 4:
		await get_tree().physics_frame
	var aim := camera.unproject_position(terminal.global_position)
	for pressed in [true, false]:
		var click := InputEventMouseButton.new()
		click.position = aim
		click.button_index = MOUSE_BUTTON_LEFT
		click.pressed = pressed
		get_viewport().push_input(click, true)
		for i in 2:
			await get_tree().physics_frame
	check(terminal.is_engaged() and actor.is_control_locked(), "Camera ray and terminal focus")
	for letter in COMMAND:
		key(letter.to_upper().unicode_at(0) as Key, letter.unicode_at(0))
	check(terminal.get_command_line().text == COMMAND, "Terminal typing")
	key(KEY_ENTER)
	var audit := CommandBus.get_audit_log()
	check(audit.size() == 1 and audit.back().ok and audit.back().command_string == COMMAND and String(audit.back().caller_id).begins_with("terminal_"), "Terminal command audit")
	key(KEY_ESCAPE)
	check(not terminal.is_engaged() and not actor.is_control_locked() and locks == [true, false], "Terminal release")
	var intermediate := false
	for i in 120:
		await get_tree().process_frame
		var strength := WorldState.get_float(&"arrival.signal")
		intermediate = intermediate or (strength > 0.0 and strength < 1.0)
		if not finished.is_empty() and events.has(&"arrival.debrief"):
			break
	var signal_end := WorldState.get_float(&"arrival.signal")
	var focus_value := camera.fov if first_person else camera.size
	check(intermediate and is_equal_approx(signal_end, 1.0), "World state interpolation")
	check(is_equal_approx(focus_value, 50.0 if first_person else 4.0), "Host camera response")
	check(finished == [&"arrival"], "AnimationPlayer finish count")
	check(actions == [&"play_animation", &"focus_beacon"], "Actor-command count")
	check(events == [&"arrival.beacon", &"arrival.debrief"], "Animation cue and mission completion hook")
	check(WorldState.get_bool(&"arrival.received"), "Lifecycle sequence wait release")
	check(MissionRunner.is_objective_completed(&"arrival", &"locate_beacon") and MissionRunner.get_mission_status(&"arrival") == MissionRunner.STATUS_COMPLETED, "Mission objective transition")
	var status := terminal.execute_command("mission.status")
	check(status.ok and "locate_beacon" in status.output, "Read completed mission via terminal")
	var repeated := terminal.execute_command("mission.complete arrival")
	var unknown := terminal.execute_command("mission.start unknown")
	await get_tree().process_frame
	check(repeated.ok and events.count(&"arrival.debrief") == 1, "Repeat completion played the outro again")
	check(not unknown.ok and unknown.error_code == ERR_DOES_NOT_EXIST, "Unknown mission diagnostic")
	var report := {
		"host": "first_person" if first_person else "rts", "rig": $Rig.get_class(),
		"camera_projection": "perspective" if first_person else "orthogonal", "focus_value": focus_value,
		"command": COMMAND, "terminal_locks": locks, "animation_finished": finished,
		"actions": actions, "events": events, "interpolated": intermediate, "signal_end": signal_end,
		"wait_released": WorldState.get_bool(&"arrival.received"), "completed_objectives": MissionRunner.get_completed_objectives(&"arrival"),
		"terminal_status_ok": status.ok, "repeat_complete_ok": repeated.ok, "unknown_mission_error": unknown.error_code,
		"failures": failures,
	}
	print("GATE3_RESULT ", JSON.stringify(report))
	get_tree().quit(0 if failures.is_empty() else 1)
