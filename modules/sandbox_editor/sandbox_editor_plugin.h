#pragma once

#include "editor/plugins/editor_plugin.h"

class SandboxEditorDock;
class SandboxEditorDebuggerPlugin;
class SandboxExportPlugin;
class SandboxInspectorPlugin;

class SandboxEditorPlugin : public EditorPlugin {
	GDCLASS(SandboxEditorPlugin, EditorPlugin);

	SandboxEditorDock *dock = nullptr;
	Ref<SandboxEditorDebuggerPlugin> debugger;
	Ref<SandboxInspectorPlugin> inspector;
	Ref<SandboxExportPlugin> exporter;

protected:
	void _notification(int p_what);
	static void _bind_methods() {}

public:
	String get_plugin_name() const override { return "Sandbox"; }
	bool handles(Object *p_object) const override;
	void edit(Object *p_object) override;
	AfterGUIInput forward_3d_gui_input(Camera3D *p_camera, const Ref<InputEvent> &p_event) override;
	SandboxEditorPlugin();
	~SandboxEditorPlugin();
};
