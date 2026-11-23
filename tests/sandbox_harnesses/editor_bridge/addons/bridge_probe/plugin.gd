@tool
extends EditorPlugin

var errors: Array[String] = []
var observations := {}

func _enter_tree() -> void:
	if OS.get_environment("SANDBOX_BRIDGE_REPORT") != "":
		_run.call_deferred()

func _wait(check: Callable, seconds: float = 12.0) -> bool:
	var end := Time.get_ticks_msec() + int(seconds * 1000)
	while Time.get_ticks_msec() < end:
		if check.call():
			return true
		await get_tree().create_timer(0.05).timeout
	return check.call()

func _verify(ok: bool, label: String) -> void:
	observations[label] = ok
	if not ok:
		errors.append(label)

func _row_value(tree: Tree, key: String) -> String:
	var row := tree.get_root()
	while row:
		if row.get_text(0) == key:
			return row.get_text(1)
		row = row.get_next_in_tree()
	return ""

func _finish() -> void:
	if EditorInterface.is_playing_scene():
		EditorInterface.stop_playing_scene()
		await get_tree().create_timer(0.5).timeout
	var file := FileAccess.open(OS.get_environment("SANDBOX_BRIDGE_REPORT"), FileAccess.WRITE)
	file.store_string(JSON.stringify({"checks": observations, "errors": errors}, "  "))
	file.close()
	get_tree().quit(0 if errors.is_empty() else 1)

func _run() -> void:
	await get_tree().create_timer(1).timeout
	var docks := get_tree().root.find_children("*", "SandboxEditorDock", true, false)
	_verify(docks.size() == 1, "one native Sandbox dock")
	if docks.size() != 1:
		await _finish()
		return
	var dock: EditorDock = docks[0]
	var tabs: TabContainer = dock.find_children("*", "TabContainer", true, false)[0]
	_verify(tabs.get_tab_count() == 5, "five editor tabs")
	var assets: ItemList = tabs.get_tab_control(0).find_children("*", "ItemList", true, false)[0]
	var names: Array[String] = []
	for index in assets.item_count:
		names.append(assets.get_item_text(index))
	_verify(["sequence.tres", "mission.tres", "schema.tres", "module.tres", "partition.tres"].all(func(path): return path in names), "asset index includes all five resource types")
	EditorInterface.edit_resource(load("res://sequence.tres"))
	await get_tree().process_frame
	var graph := tabs.get_tab_control(1)
	var node := graph.find_child("node_" + "hold".to_utf8_buffer().hex_encode(), true, false) as GraphNode
	_verify(node != null and tabs.current_tab == 1, "inspection opens the live sequence")
	if not node:
		await _finish()
		return
	EditorInterface.play_main_scene()
	_verify(await _wait(func(): return node.get_meta(&"trace_state", "") == "awaiting", 35), "game debugger highlights awaiting graph node")
	var console := tabs.get_tab_control(4)
	var command: LineEdit = console.find_children("*", "LineEdit", true, false)[0]
	var suggestions: ItemList = console.find_children("*", "ItemList", true, false)[0]
	var output: RichTextLabel = console.find_children("*", "RichTextLabel", true, false)[0]
	_verify(command.editable, "game session enables command input")
	tabs.current_tab = 4
	command.text = "state.g"
	command.text_changed.emit(command.text)
	_verify(await _wait(func(): return suggestions.item_count > 0 and suggestions.get_item_text(0) == "state.get"), "remote autocomplete returns state.get")
	command.text_submitted.emit("state.get fixture.pid")
	_verify(await _wait(func(): return output.get_parsed_text().strip_edges().get_slice("\n", 1).is_valid_int()), "runtime command returns PID")
	var runtime_pid := output.get_parsed_text().strip_edges().get_slice("\n", 1)
	_verify(runtime_pid.is_valid_int() and int(runtime_pid) != OS.get_process_id(), "console reaches a separate game process")
	dock.make_visible()
	tabs.current_tab = 2
	var live: Tree = tabs.get_tab_control(2).find_children("*", "Tree", true, false)[0]
	await get_tree().process_frame
	_verify(live.is_visible_in_tree(), "live inspector visible during sampling")
	_verify(await _wait(func(): return runtime_pid != "" and _row_value(live, "fixture.pid") == runtime_pid), "live telemetry reports matching game PID")
	_verify(_row_value(live, "renderer_video_memory_bytes").is_valid_int() and _row_value(live, "Agents").is_valid_int(), "streaming and simulation telemetry received")
	_verify(_row_value(live, "fixture.clock").contains("last_tick_duration_usec"), "registered simulation module reports tick time")
	_verify(["tier0", "tier1", "tier2"].all(func(key): return _row_value(live, key).is_valid_int()), "all three hydration counts displayed")
	var map: TextureRect = tabs.get_tab_control(2).get_child(-1)
	_verify(await _wait(func(): return _row_value(live, "resident_cells") == "1" and map.texture != null), "resident cell heatmap populated")
	if not errors.is_empty():
		var row := live.get_root()
		while row:
			print("Live State ", row.get_text(0), " = ", row.get_text(1))
			row = row.get_next_in_tree()
		print("Heatmap texture ", map.texture)
	await _finish()
