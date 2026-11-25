#include "sandbox_editor_debugger.h"

#include "core/object/callable_mp.h"

void SandboxEditorDebuggerPlugin::_bind_methods() {
	ADD_SIGNAL(MethodInfo("session_changed", PropertyInfo(Variant::BOOL, "started"), PropertyInfo(Variant::INT, "session")));
	ADD_SIGNAL(MethodInfo("packet_received", PropertyInfo(Variant::STRING, "message"), PropertyInfo(Variant::ARRAY, "data"), PropertyInfo(Variant::INT, "session")));
}

void SandboxEditorDebuggerPlugin::_session_changed(bool p_started, int p_id) {
	emit_signal(SNAME("session_changed"), p_started, p_id);
}

void SandboxEditorDebuggerPlugin::setup_session(int p_idx) {
	Ref<EditorDebuggerSession> session = get_session(p_idx);
	session->connect("started", callable_mp(this, &SandboxEditorDebuggerPlugin::_session_changed).bind(true, p_idx));
	session->connect("stopped", callable_mp(this, &SandboxEditorDebuggerPlugin::_session_changed).bind(false, p_idx));
}

bool SandboxEditorDebuggerPlugin::capture(const String &p_message, const Array &p_data, int p_session) {
	if (p_message != "sandbox:telemetry" && p_message != "sandbox:result" && p_message != "sandbox:completions" && p_message != "sandbox:narrative_trace") {
		return false;
	}
	emit_signal(SNAME("packet_received"), p_message, p_data, p_session);
	return true;
}

bool SandboxEditorDebuggerPlugin::send(int p_session, const String &p_message, const Array &p_data) {
	if (p_session < 0 || p_session >= get_sessions().size()) {
		return false;
	}
	Ref<EditorDebuggerSession> session = get_session(p_session);
	if (!session->is_active()) {
		return false;
	}
	session->send_message("sandbox:" + p_message, p_data);
	return true;
}
