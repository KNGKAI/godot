#include "sandbox_runtime_debugger.h"

#ifdef DEBUG_ENABLED
#include "core/debugger/engine_debugger.h"
#include "core/os/thread.h"

#include "modules/modules_enabled.gen.h"
#include "modules/sandbox_core/command_bus.h"
#include "modules/sandbox_core/event_bus.h"
#include "modules/sandbox_core/world_state.h"
#include "modules/sandbox_narrative/mission_runner.h"
#ifdef MODULE_SANDBOX_SIM_ENABLED
#include "modules/sandbox_sim/simulation_server.h"
#endif
#ifdef MODULE_SANDBOX_BUBBLE_ENABLED
#include "modules/sandbox_bubble/hydration_bubble.h"
#endif
#ifdef MODULE_SANDBOX_STREAMING_ENABLED
#include "modules/sandbox_streaming/streaming_server.h"
#endif

namespace SandboxRuntimeDebugger {
static Dictionary snapshot() {
	Dictionary packet;
	Dictionary scalars;
	WorldState *world = WorldState::get_singleton();
	for (const String &key : world->get_all_keys()) {
		scalars[key] = world->get_value(key).stringify();
	}
	packet["World state"] = scalars;
	packet["Missions"] = MissionRunner::get_singleton()->export_state();
	packet["Entity rows"] = world->get_entity_count();
#ifdef MODULE_SANDBOX_SIM_ENABLED
	SimulationServer *sim = SimulationServer::get_singleton();
	packet["Simulation modules"] = sim->get_debug_module_metrics();
	packet["Agents"] = sim->get_agent_count();
#endif
#ifdef MODULE_SANDBOX_BUBBLE_ENABLED
	packet["Hydration"] = HydrationBubble::get_singleton()->get_stats();
#endif
#ifdef MODULE_SANDBOX_STREAMING_ENABLED
	packet["Streaming"] = StreamingServer::get_singleton()->get_stats();
	packet["cells"] = StreamingServer::get_singleton()->get_debug_cells();
#endif
	PackedStringArray events;
	const TypedArray<Dictionary> history = EventBus::get_singleton()->get_history();
	for (int i = MAX(0, history.size() - 32); i < history.size(); i++) {
		Dictionary entry = history[i];
		events.push_back(vformat("%s  %s  %s", entry["timestamp_usec"], entry["event_id"], entry["payload"].stringify().left(256)));
	}
	packet["Recent events"] = events;
	return packet;
}

static Error capture(void *, const String &p_message, const Array &p_args, bool &r_captured) {
	r_captured = p_message == "cmd" || p_message == "complete" || p_message == "snapshot";
	if (!r_captured) {
		return OK;
	}
	if (!Thread::is_main_thread()) {
		return ERR_BUSY;
	}
	EngineDebugger *debugger = EngineDebugger::get_singleton();
	if (p_message == "snapshot") {
		ERR_FAIL_COND_V(!p_args.is_empty(), ERR_INVALID_PARAMETER);
		debugger->send_message("sandbox:telemetry", { snapshot() });
		return OK;
	}
	ERR_FAIL_COND_V(p_args.size() != 1 || p_args[0].get_type() != Variant::STRING, ERR_INVALID_PARAMETER);
	String input = p_args[0];
	CommandBus *bus = CommandBus::get_singleton();
	if (p_message == "cmd") {
		Dictionary result = bus->execute(input, CommandBus::PERMISSION_DEV, SNAME("editor_debugger"));
		debugger->send_message("sandbox:result", { result });
	} else {
		ERR_FAIL_COND_V(input.utf8().length() > CommandBus::MAX_COMMAND_BUFFER_BYTES, ERR_INVALID_PARAMETER);
		debugger->send_message("sandbox:completions", { input, bus->get_autocomplete_suggestions(input, CommandBus::PERMISSION_DEV) });
	}
	return OK;
}

void initialize() {
	EngineDebugger::register_message_capture("sandbox", EngineDebugger::Capture(nullptr, capture));
}

void deinitialize() {
	EngineDebugger::unregister_message_capture("sandbox");
}
} // namespace SandboxRuntimeDebugger
#endif
