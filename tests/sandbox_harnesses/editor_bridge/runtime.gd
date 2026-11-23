extends Node

func _ready() -> void:
	var sequence := load("res://sequence.tres") as SequenceData
	var mission := load("res://mission.tres") as MissionData
	if "--check-package" in OS.get_cmdline_user_args():
		var valid := sequence != null and mission != null and sequence.get_editor_graph_meta().is_empty() and mission.get_editor_graph_meta().is_empty() and sequence.entry_step_uuid == &"hold" and mission.on_start_sequence == sequence and sequence.steps.size() == 1
		print("SANDBOX_RELEASE_LAYOUT_STRIPPED=", valid)
		get_tree().quit(0 if valid else 1)
		return
	WorldState.set_string(&"fixture.pid", str(OS.get_process_id()))
	Engine.get_singleton("SimulationServer").register_module(load("res://module.tres"))
	var stream := Engine.get_singleton("StreamingServer")
	stream.set_manifest(load("res://partition.tres"))
	add_child(StreamingInvoker.new())
	stream.set_enabled(true)
	var director := SequenceDirector.new()
	add_child(director)
	director.play_sequence(sequence)
