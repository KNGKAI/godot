extends Node

var control := TCPServer.new()
var started := 0
var results := {}
var closed := false


func save_report(filename: String) -> void:
	var file := FileAccess.open("res://" + filename, FileAccess.WRITE)
	file.store_string(JSON.stringify(results, "\t"))
	file.close()


func control_command() -> String:
	return "control"


func _ready() -> void:
	started = Time.get_ticks_msec()
	var port := int(OS.get_cmdline_user_args()[0])
	var server = Engine.get_singleton("SandboxDebugServer")
	var bus = Engine.get_singleton("CommandBus")
	var signature: Array[int] = []
	var listen_result: int = server.start(port, PackedStringArray(["http://localhost"]))
	# Use the numeric DEV tier so the probe also works if release omits the enum binding.
	bus.register_command("probe.dev", 3, signature, control_command)
	bus.register_command("probe.user", 1, signature, control_command)
	results = {
		"pid": OS.get_process_id(),
		"release": OS.has_feature("release") and not OS.is_debug_build(),
		"requested_port": port,
		"listener_result": listen_result,
		"listening": server.is_listening(),
		"reported_port": server.get_local_port(),
		"token_empty": server.get_session_bearer_token().is_empty(),
		"builtin_dev_registered": bus.has_command("dev.inspect"),
		"custom_dev_registered": bus.has_command("probe.dev"),
		"builtin_as_dev": bus.execute_args("dev.inspect", [], 3),
		"builtin_as_admin": bus.execute("dev.inspect", 2),
		"custom_as_admin": bus.execute("probe.dev", 2),
		"user_as_dev": bus.execute("probe.user", 3),
		"user_as_user": bus.execute("probe.user", 1),
	}
	var command_checks_passed := true
	for result in [results.builtin_as_dev, results.builtin_as_admin, results.custom_as_admin, results.user_as_dev]:
		command_checks_passed = command_checks_passed and not result.ok and result.error_code == ERR_UNAUTHORIZED
	for result in [results.builtin_as_admin, results.custom_as_admin]:
		command_checks_passed = command_checks_passed and "Unknown command" in result.output
	results.passed = (
		results.release and listen_result == ERR_UNAUTHORIZED and not results.listening
		and results.reported_port == 0 and results.token_empty and command_checks_passed
		and not results.builtin_dev_registered and not results.custom_dev_registered
		and results.user_as_user.ok and results.user_as_user.output == "control"
	)
	# A normal listener verifies that the host can observe sockets owned by this exact engine process.
	results.control_started = control.listen(0, "127.0.0.1") == OK
	results.control_port = control.get_local_port()
	save_report("listening.json")


func _process(_delta: float) -> void:
	if not closed and FileAccess.file_exists("res://close-control"):
		control.stop()
		closed = true
		save_report("closed.json")
	if closed and FileAccess.file_exists("res://finish"):
		get_tree().quit(0 if results.passed else 1)
	if Time.get_ticks_msec() - started > 60000:
		get_tree().quit(2)
