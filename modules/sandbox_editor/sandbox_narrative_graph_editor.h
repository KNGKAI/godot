#pragma once

#include "core/io/resource.h"
#include "core/templates/hash_map.h"
#include "core/variant/typed_array.h"
#include "scene/gui/box_container.h"

class GraphEdit;
class GraphElement;
class GraphFrame;
class GraphNode;
class MenuButton;
class FileDialog;
class UndoRedo;

class SandboxNarrativeGraphEditor : public VBoxContainer {
	GDCLASS(SandboxNarrativeGraphEditor, VBoxContainer);

	enum PortType { FLOW,
		LOGIC,
		SEQUENCE };

	Ref<Resource> edited;
	GraphEdit *canvas = nullptr;
	MenuButton *palette = nullptr;
	FileDialog *sequence_dialog = nullptr;
	HashMap<StringName, Ref<Resource>> resources;
	HashMap<ObjectID, StringName> node_keys;
	HashMap<StringName, GraphElement *> elements;
	Dictionary traces;
	bool refresh_pending = false;
	bool editing_field = false;
	UndoRedo *standalone_history = nullptr;

	void queue_refresh();
	void refresh();
	void configure_palette();
	void palette_selected(int p_index);
	void load_sequence(const String &p_path);
	Vector2 insertion_position() const;
	void add_resource(const Ref<Resource> &p_node, const Vector2 &p_position);
	StringName show_resource(const Ref<Resource> &p_node, const String &p_key);
	void show_frame(const StringName &p_key, const Dictionary &p_layout);
	void add_fields(GraphNode *p_node, const Ref<Resource> &p_resource);
	void wire(const StringName &p_from, int p_output, const StringName &p_to, int p_input);
	void select_resource(Node *p_node);
	void change_field(const StringName &p_property, const Variant &p_value, const StringName &p_field, bool p_changing, const Ref<Resource> &p_resource);
	void commit(const String &p_name, const Dictionary &p_changes);
	void request_link(const StringName &p_from, int p_output, const StringName &p_to, int p_input, bool p_connect);
	void change_link(Dictionary &r_changes, const StringName &p_from, int p_output, const StringName &p_to, int p_input, bool p_connect);
	void remove_nodes(const TypedArray<StringName> &p_nodes);
	void save_layout();
	void attach_nodes(const TypedArray<StringName> &p_names, const StringName &p_frame);
	void rename_frame(const String &p_value, const StringName &p_key);
	void resize_frame(const Vector2 &p_size, GraphFrame *p_frame);
	void update_trace();

protected:
	static void _bind_methods();

public:
	void edit(const Ref<Resource> &p_resource);
	Ref<Resource> get_edited_resource() const { return edited; }
	GraphEdit *get_graph_edit() const { return canvas; }
	void apply_trace(const Array &p_trace);
	void clear_trace();
	StringName add_graph_node(const StringName &p_class, const Vector2 &p_position);
	explicit SandboxNarrativeGraphEditor(UndoRedo *p_standalone_history = nullptr);
};
