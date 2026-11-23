@tool
extends EditorPlugin

var graph: Control
var canvas: GraphEdit
var failed := false
var checks := 0

func _enter_tree() -> void:
	if "--graph-probe" in OS.get_cmdline_user_args():
		run.call_deferred()

func check(value: bool, description: String) -> void:
	checks += 1
	if not value:
		failed = true
		push_error("Gate 8 graph: " + description)
		get_tree().quit(1)

func settle() -> void:
	await get_tree().process_frame
	await get_tree().process_frame

func add(kind: StringName, offset: Vector2 = Vector2(100, 100)) -> StringName:
	var key: StringName = graph.call("add_graph_node", kind, offset)
	await settle()
	check(not key.is_empty() and canvas.has_node(NodePath(key)), "add " + kind)
	return key

func link_nodes(source: StringName, port: int, target: StringName, input: int) -> void:
	canvas.connection_request.emit(source, port, target, input)
	await settle()
	var output_node: GraphNode = canvas.get_node(NodePath(source))
	var input_node: GraphNode = canvas.get_node(NodePath(target))
	check(canvas.is_node_connected(source, port, target, input), "connect %s:%d (%d outputs) to %s:%d (%d inputs)" % [source, port, output_node.get_output_port_count(), target, input, input_node.get_input_port_count()])

func scene_menu_action(action: String) -> void:
	for menu: PopupMenu in EditorInterface.get_base_control().get_tree().root.find_children("*", "PopupMenu", true, false):
		var index := menu.get_item_index(menu.get_item_id(0)) if menu.item_count else -1
		var has_save := false
		var action_id := -1
		while index >= 0 and index < menu.item_count:
			var label := menu.get_item_text(index)
			has_save = has_save or label == "Save Scene"
			if label == action or label.begins_with(action + " "):
				action_id = menu.get_item_id(index)
			index += 1
		if has_save and action_id >= 0:
			menu.id_pressed.emit(action_id)
			await settle()
			return
	check(false, "find " + action + " in the editor Scene menu")

func run() -> void:
	await settle()
	var candidates := EditorInterface.get_base_control().find_children("*", "SandboxNarrativeGraphEditor", true, false)
	check(candidates.size() == 1, "one native narrative editor")
	if candidates.is_empty():
		get_tree().quit(1)
		return
	graph = candidates.front()
	canvas = graph.call("get_graph_edit")
	var mission := MissionData.new()
	mission.mission_id = &"graph_harness"
	EditorInterface.edit_resource(mission)
	await settle()
	check(graph.call("get_edited_resource") == mission, "resource opened via EditorInterface")
	var a := await add("ObjectiveData")
	var b := await add("ObjectiveData")
	var c := await add("ObjectiveData")
	var all := await add("CondAll")
	var any := await add("CondAny")
	var event := await add("CondEventFired")
	var state := await add("CondStateRange")
	var negate := await add("CondNot")
	var ref_node := await add("SequenceData")
	var frame := await add("GraphFrame")
	for i in 4:
		await add("ObjectiveData", Vector2(50, 180 * i))
	await link_nodes(a, 0, b, 0)
	await link_nodes(a, 1, c, 0)
	await link_nodes(event, 0, any, 0)
	await link_nodes(state, 0, any, 0)
	await link_nodes(any, 0, all, 0)
	await link_nodes(all, 0, a, 1)
	await link_nodes(event, 0, negate, 0)
	await link_nodes(negate, 0, a, 2)
	await link_nodes(&"entry", 1, ref_node, 0)
	canvas.arrange_nodes()
	await settle()
	check(canvas.get_node(NodePath(a)).position_offset != canvas.get_node(NodePath(b)).position_offset, "layered auto-arrange separates connected objectives")
	check(mission.get_editor_graph_meta()[a]["position"] == canvas.get_node(NodePath(a)).position_offset, "arranged positions persisted")
	check(mission.objectives.size() == 7 and mission.transitions.size() == 2, "authored objectives and transitions")
	check(mission.transitions[1].on_fail, "failure transition")
	check(mission.objectives[0].complete_condition.children[0].children.size() == 2, "nested boolean conditions")
	var sequence: SequenceData = mission.on_start_sequence
	check(sequence != null, "mission lifecycle subresource")
	var objective: ObjectiveData = mission.objectives[0]
	var native_title: EditorProperty
	for prop: EditorProperty in canvas.get_node(NodePath(a)).find_children("*", "EditorProperty", true, false):
		if prop.get_edited_property() == &"title":
			native_title = prop
			break
	check(native_title != null, "inline title editor")
	if native_title:
		native_title.emit_changed(&"title", "Open the gate")
		await settle()
		check(objective.title == "Open the gate", "inline persistent title")
	objective.title = "Inspector update"
	await settle()
	check(canvas.get_node(NodePath(a)).title == "Inspector update", "live resource-to-canvas sync")
	var remove: Array[StringName] = [a, frame]
	canvas.delete_nodes_request.emit(remove)
	await settle()
	check(mission.objectives.size() == 6 and mission.transitions.is_empty(), "atomic multi-delete")
	var manager := get_undo_redo()
	var history := manager.get_history_undo_redo(manager.get_object_history_id(mission))
	check(history.get_current_action_name() == "Delete Narrative Nodes", "record in editor manager history")
	await scene_menu_action("Undo")
	check(mission.objectives.size() == 7 and mission.transitions.size() == 2 and canvas.has_node(NodePath(frame)), "Scene menu undo restores nodes, edges and frame")
	await scene_menu_action("Redo")
	check(mission.objectives.size() == 6 and mission.transitions.is_empty(), "Scene menu redo deletes nodes and edges")
	await scene_menu_action("Undo")
	var open_sequence: Button
	for child in canvas.get_node(NodePath(ref_node)).get_children():
		if child is Button and child.text == "Edit Sequence":
			open_sequence = child
			break
	check(open_sequence != null, "lifecycle subasset opens from its graph node")
	if open_sequence == null:
		return
	open_sequence.pressed.emit()
	await settle()
	check(graph.call("get_edited_resource") == sequence, "open linked sequence")
	var fork := await add("SequenceAction")
	var branch := await add("ActionBranchCondition")
	var wait := await add("ActionWaitEvent")
	var yes := await add("ActionSetState")
	var no := await add("ActionEmitEvent")
	var condition := await add("CondStateEquals")
	await link_nodes(fork, 0, branch, 0)
	await link_nodes(fork, 0, wait, 0)
	await link_nodes(branch, 0, yes, 0)
	await link_nodes(branch, 1, no, 0)
	await link_nodes(condition, 0, branch, 1)
	check(sequence.steps[0].next_steps.size() == 2, "parallel fork")
	check(sequence.steps[1].true_steps.size() == 1 and sequence.steps[1].false_steps.size() == 1, "guarded branches")
	var path := "user://gate8-authored-mission.tres"
	check(ResourceSaver.save(mission, path) == OK, "save authored mission")
	var reopened := ResourceLoader.load(path, "", ResourceLoader.CACHE_MODE_IGNORE) as MissionData
	check(reopened != null and reopened.objectives.size() == 7 and reopened.on_start_sequence.steps.size() == 5, "reopen authored mission and nested sequence")
	print("Gate 8 native graph authoring: checks=%d failed=%s" % [checks, failed])
	get_tree().quit(1 if failed else 0)
