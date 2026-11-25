#pragma once

#include "core/io/resource.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"

#include "modules/sandbox_narrative/sequence_action.h"

class SequenceData : public Resource {
	GDCLASS(SequenceData, Resource);

private:
	StringName sequence_id;
	StringName entry_step_uuid;
	bool use_first_as_entry = true;
	TypedArray<SequenceAction> steps;
	Dictionary _editor_graph_meta;

protected:
	static void _bind_methods();

public:
	void set_sequence_id(const StringName &p_id) {
		sequence_id = p_id;
		emit_changed();
	}
	StringName get_sequence_id() const { return sequence_id; }

	void set_entry_step_uuid(const StringName &p_uuid) {
		entry_step_uuid = p_uuid;
		emit_changed();
	}
	StringName get_entry_step_uuid() const { return entry_step_uuid; }

	void set_use_first_as_entry(bool p_enabled) {
		use_first_as_entry = p_enabled;
		emit_changed();
	}
	bool is_using_first_as_entry() const { return use_first_as_entry; }

	void set_steps(const TypedArray<SequenceAction> &p_steps) {
		steps = p_steps;
		emit_changed();
	}
	TypedArray<SequenceAction> get_steps() const { return steps; }

	void set_editor_graph_meta(const Dictionary &p_meta) {
		_editor_graph_meta = p_meta;
		emit_changed();
	}
	Dictionary get_editor_graph_meta() const { return _editor_graph_meta; }

	Ref<SequenceAction> find_step(const StringName &p_step_uuid) const;
};
