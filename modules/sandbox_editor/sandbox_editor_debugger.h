#pragma once

#include "editor/debugger/editor_debugger_plugin.h"

class SandboxEditorDebuggerPlugin : public EditorDebuggerPlugin {
	GDCLASS(SandboxEditorDebuggerPlugin, EditorDebuggerPlugin);

	void _session_changed(bool p_started, int p_id);

protected:
	static void _bind_methods();

public:
	void setup_session(int p_idx) override;
	bool has_capture(const String &p_capture) const override { return p_capture == "sandbox"; }
	bool capture(const String &p_message, const Array &p_data, int p_session) override;
	bool send(int p_session, const String &p_message, const Array &p_data = Array());
};
