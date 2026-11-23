#pragma once

#include "editor/inspector/editor_inspector.h"

#include "modules/sandbox_core/sandbox_schema_registry.h"

class Button;
class Camera3D;
class Label;
class LineEdit;
class PopupPanel;
class SandboxInspectorPlugin;
class Tree;

struct SandboxIdHint {
	enum Kind { NONE,
		STATE,
		EVENT,
		ACTOR,
		MARKER };
	Kind kind = NONE;
	Variant::Type expected = Variant::NIL;

	static SandboxIdHint parse(const String &p_hint);
	SandboxSchemaRegistry::ValidationStatus validate(const Ref<SandboxSchemaRegistry> &p_schema, const StringName &p_id) const;
	PackedStringArray search(const Ref<SandboxSchemaRegistry> &p_schema, const String &p_filter) const;
};

class SandboxIdPickerProperty : public EditorProperty {
	GDCLASS(SandboxIdPickerProperty, EditorProperty);

	SandboxIdHint hint;
	Variant::Type property_type = Variant::STRING_NAME;
	Ref<SandboxSchemaRegistry> schema;
	Button *field = nullptr;
	Button *pick = nullptr;
	Button *focus = nullptr;
	Button *add = nullptr;
	PopupPanel *popup = nullptr;
	LineEdit *filter = nullptr;
	Tree *choices = nullptr;
	Label *status = nullptr;

	void _open();
	void _filter(const String &p_text);
	void _choose();
	void _submit(const String &p_id);
	void _focus();
	void _add_to_schema();

protected:
	static void _bind_methods() {}
	void _notification(int p_what);
	void _set_read_only(bool p_read_only) override;

public:
	void setup(SandboxInspectorPlugin *p_plugin, SandboxIdHint p_hint, const Ref<SandboxSchemaRegistry> &p_schema, Variant::Type p_property_type = Variant::STRING_NAME);
	void update_property() override;
	bool pick_at(Node *p_root, Camera3D *p_camera, const Point2 &p_position);
	SandboxIdPickerProperty();
};

class SandboxInspectorPlugin : public EditorInspectorPlugin {
	GDCLASS(SandboxInspectorPlugin, EditorInspectorPlugin);

	ObjectID armed_picker;
	Ref<SandboxSchemaRegistry> schema;

protected:
	static void _bind_methods() {}
	bool can_handle(Object *p_object) override;
	void parse_begin(Object *p_object) override;
	bool parse_property(Object *p_object, Variant::Type p_type, const String &p_path, PropertyHint p_hint, const String &p_hint_text, BitField<PropertyUsageFlags> p_usage, bool p_wide = false) override;

public:
	static Ref<SandboxSchemaRegistry> discover_schema(Node *p_scene_root);
	void arm_picker(ObjectID p_picker);
	bool forward_3d_gui_input(Camera3D *p_camera, const Ref<InputEvent> &p_event);
};
