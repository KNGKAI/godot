#include "sandbox_editor_plugin.h"

#include "sandbox_editor_debugger.h"
#include "sandbox_editor_dock.h"
#include "sandbox_export_plugin.h"
#include "sandbox_inspector_plugin.h"

#include "modules/sandbox_narrative/mission_data.h"

bool SandboxEditorPlugin::handles(Object *p_object) const {
	return Object::cast_to<MissionData>(p_object) || Object::cast_to<SequenceData>(p_object);
}

void SandboxEditorPlugin::edit(Object *p_object) {
	if (handles(p_object)) {
		dock->edit(Ref<Resource>(Object::cast_to<Resource>(p_object)));
	}
}

EditorPlugin::AfterGUIInput SandboxEditorPlugin::forward_3d_gui_input(Camera3D *p_camera, const Ref<InputEvent> &p_event) {
	return inspector->forward_3d_gui_input(p_camera, p_event) ? AFTER_GUI_INPUT_STOP : AFTER_GUI_INPUT_PASS;
}

void SandboxEditorPlugin::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		if (!is_input_event_forwarding_always_enabled()) {
			set_input_event_forwarding_always_enabled();
		}
		dock = memnew(SandboxEditorDock);
		dock->set_bridge(debugger);
		add_dock(dock);
		add_inspector_plugin(inspector);
		add_export_plugin(exporter);
		add_debugger_plugin(debugger);
	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		remove_debugger_plugin(debugger);
		remove_export_plugin(exporter);
		remove_inspector_plugin(inspector);
		remove_dock(dock);
		memdelete(dock);
		dock = nullptr;
	}
}

SandboxEditorPlugin::SandboxEditorPlugin() {
	debugger.instantiate();
	inspector.instantiate();
	exporter.instantiate();
}

SandboxEditorPlugin::~SandboxEditorPlugin() {
}
