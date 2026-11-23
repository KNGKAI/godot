#pragma once

#include "editor/export/editor_export_plugin.h"

#include "modules/sandbox_core/sandbox_schema_registry.h"

class SandboxExportPlugin : public EditorExportPlugin {
	GDCLASS(SandboxExportPlugin, EditorExportPlugin);

protected:
	static void _bind_methods() {}
	Error _export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) override;
	bool _begin_customize_resources(const Ref<EditorExportPlatform> &p_platform, const Vector<String> &p_features) override;
	Ref<Resource> _customize_resource(const Ref<Resource> &p_resource, const String &p_path) override;
	uint64_t _get_customization_configuration_hash() const override { return 1; }

public:
	String get_name() const override { return "SandboxSchema"; }
	bool supports_platform(const Ref<EditorExportPlatform> &p_platform) const override { return true; }
	static PackedStringArray audit_resource(const Ref<Resource> &p_resource, const Ref<SandboxSchemaRegistry> &p_schema);
};
