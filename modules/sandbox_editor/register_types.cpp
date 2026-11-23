#include "register_types.h"

#include "sandbox_editor_debugger.h"
#include "sandbox_editor_dock.h"
#include "sandbox_editor_plugin.h"
#include "sandbox_export_plugin.h"
#include "sandbox_hlod_baker.h"
#include "sandbox_inspector_plugin.h"
#include "sandbox_narrative_graph_editor.h"

void initialize_sandbox_editor_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		GDREGISTER_INTERNAL_CLASS(SandboxEditorDock);
		GDREGISTER_INTERNAL_CLASS(SandboxNarrativeGraphEditor);
		GDREGISTER_INTERNAL_CLASS(SandboxHLODBaker);
		GDREGISTER_INTERNAL_CLASS(SandboxIdPickerProperty);
		GDREGISTER_INTERNAL_CLASS(SandboxInspectorPlugin);
		GDREGISTER_INTERNAL_CLASS(SandboxExportPlugin);
		GDREGISTER_INTERNAL_CLASS(SandboxEditorDebuggerPlugin);
		GDREGISTER_INTERNAL_CLASS(SandboxEditorPlugin);
		EditorPlugins::add_by_type<SandboxEditorPlugin>();
	}
}

void uninitialize_sandbox_editor_module(ModuleInitializationLevel p_level) {
}
