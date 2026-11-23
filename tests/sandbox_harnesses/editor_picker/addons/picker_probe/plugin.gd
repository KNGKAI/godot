@tool
extends EditorPlugin

var checks := {}
var coordinates := {}

func _enter_tree() -> void:
	if not OS.get_environment("SANDBOX_PICKER_REPORT").is_empty():
		_run.call_deferred()

func _frames(count: int) -> void:
	for i in count:
		await get_tree().process_frame

func _wait(predicate: Callable, seconds := 12.0) -> bool:
	var deadline := Time.get_ticks_msec() + int(seconds * 1000)
	while not predicate.call() and Time.get_ticks_msec() < deadline:
		await get_tree().process_frame
	return predicate.call()

func _record(label: String, passed: bool) -> bool:
	checks[label] = passed
	print("Picker probe ", label, " = ", passed)
	return passed

func _finish() -> void:
	var output := FileAccess.open(OS.get_environment("SANDBOX_PICKER_REPORT"), FileAccess.WRITE)
	output.store_string(JSON.stringify({"checks": checks, "viewport_clicks": coordinates}, "  "))
	output.close()
	get_tree().quit(1 if false in checks.values() else 0)

func _picker(property: StringName) -> EditorProperty:
	for row in EditorInterface.get_inspector().find_children("*", "EditorProperty", true, false):
		if row.is_class("SandboxIdPickerProperty") and row.is_visible_in_tree() and row.get_edited_property() == property:
			return row
	return null

func _field(row: EditorProperty) -> Button:
	for button: Button in row.find_children("*", "Button", true, false):
		if button.is_visible_in_tree() and not button.text.is_empty():
			return button
	return null

func _pick(property: StringName, target: Node3D, label: String, observed: Callable) -> bool:
	if not _record(label + " native property", await _wait(func(): return _picker(property) != null)):
		return false
	var row := _picker(property)
	var eye: Button
	for button: Button in row.find_children("*", "Button", true, false):
		if button.tooltip_text.begins_with("Pick the actor or marker"):
			eye = button
	if not _record(label + " eyedropper", eye != null):
		return false
	eye.pressed.emit()
	await _frames(3)
	var vp := EditorInterface.get_editor_viewport_3d(0)
	var camera := vp.get_camera_3d()
	if not _record(label + " camera", camera != null and not camera.is_position_behind(target.global_position)):
		return false
	var point := camera.unproject_position(target.global_position)
	if not _record(label + " in viewport", vp.get_visible_rect().has_point(point)):
		return false
	# Deliver window input over the editor's viewport container, including its tool overlay.
	var container := vp.get_parent() as SubViewportContainer
	if not _record(label + " editor viewport container", container != null and container.is_visible_in_tree()):
		return false
	coordinates[label] = {"x": point.x, "y": point.y, "width": vp.size.x, "height": vp.size.y}
	var window_point := container.get_global_transform_with_canvas() * (point * container.size / Vector2(vp.size))
	for down in [true, false]:
		var click := InputEventMouseButton.new()
		click.position = window_point
		click.global_position = window_point
		click.button_index = MOUSE_BUTTON_LEFT
		click.button_mask = MOUSE_BUTTON_MASK_LEFT if down else 0
		click.pressed = down
		container.get_viewport().push_input(click, true)
		if down:
			await _frames(1)
	return _record(label + " value changed", await _wait(observed, 3.0))

func _run() -> void:
	await _frames(4)
	EditorInterface.open_scene_from_path("res://world.tscn")
	if not _record("fixture scene", await _wait(func(): return EditorInterface.get_edited_scene_root() != null)):
		_finish()
		return
	var scene := EditorInterface.get_edited_scene_root()
	var anchor: Node3D = scene.get_node("Courier")
	var marker: Marker3D = scene.get_node("Dock")
	EditorInterface.set_main_screen_editor("3D")
	EditorInterface.edit_node(scene)
	if not _record("host state picker", await _wait(func(): return _picker(&"state_key") != null and _picker(&"agent") != null)):
		_finish()
		return
	var state := _picker(&"state_key")
	_record("type mismatch badge", _field(state).tooltip_text == "Incompatible state key type" and _field(state).icon != null)
	_record("missing ID badge", _field(_picker(&"agent")).tooltip_text == "Unregistered ID" and _field(_picker(&"agent")).icon != null)
	_field(state).pressed.emit()
	var filter: LineEdit = state.find_children("*", "LineEdit", true, false)[0]
	filter.text = "dpwl"
	filter.text_changed.emit(filter.text)
	var choices: Tree = state.find_children("*", "Tree", true, false)[0]
	var district := choices.get_root().get_first_child()
	var group := district.get_first_child() if district else null
	var match_item := group.get_first_child() if group else null
	if _record("fuzzy namespace results", district != null and district.get_text(0) == "district" and group != null and group.get_text(0) == "power" and match_item != null and match_item.get_metadata(0) == "district.power.load"):
		match_item.select(0)
		choices.item_activated.emit()
		_record("typed state selection", await _wait(func(): return scene.get(&"state_key") == &"district.power.load", 3.0))
	else:
		state.find_children("*", "PopupPanel", true, false)[0].hide()
	await _pick(&"agent", anchor, "Inspector actor", func(): return scene.get(&"agent") == &"city.courier")
	await _pick(&"waypoint", marker, "Inspector marker", func(): return scene.get(&"waypoint") == &"Dock")
	var player: AnimationPlayer = scene.get_node("Player")
	EditorInterface.edit_node(player)
	var base := EditorInterface.get_base_control()
	var track_rows := func():
		return base.find_children("*", "AnimationTrackEdit", true, false).filter(func(n): return n.is_visible_in_tree())
	if not _record("custom track row", await _wait(func(): return not track_rows.call().is_empty())):
		_finish()
		return
	# The timeline control's public selection signal opens its real one-key Inspector proxy.
	track_rows.call()[0].emit_signal(&"select_key", 0, true)
	if not _record("native timeline key Inspector", await _wait(func(): return _picker(&"actor_id") != null and EditorInterface.get_inspector().get_edited_object().is_class("AnimationTrackKeyEdit"))):
		_finish()
		return
	var clip := player.get_animation(&"Command")
	await _pick(&"actor_id", anchor, "Timeline actor", func(): return clip.track_get_key_value(0, 0).actor_id == &"city.courier")
	await _pick(&"marker_id", marker, "Timeline marker", func(): return clip.track_get_key_value(0, 0).payload.marker_id == &"Dock")
	var key: Dictionary = clip.track_get_key_value(0, 0)
	_record("timeline action and sibling payload preserved", key.actor_id == &"city.courier" and key.action_id == &"host.follow_marker" and key.payload.speed == 7)
	var marker_row := _picker(&"marker_id")
	var framed := false
	if marker_row:
		for button: Button in marker_row.find_children("*", "Button", true, false):
			if button.tooltip_text.begins_with("Frame this ID"):
				button.pressed.emit()
				framed = await _wait(func(): return EditorInterface.get_selection().get_selected_nodes().has(marker), 3.0)
				break
	_record("frame marker selects target", framed)
	_finish()
