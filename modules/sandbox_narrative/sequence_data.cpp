#include "sequence_data.h"

#include "core/object/class_db.h"

Ref<SequenceAction> SequenceData::find_step(const StringName &p_step_uuid) const {
	for (int i = 0; i < steps.size(); i++) {
		Ref<SequenceAction> step = steps[i];
		if (step.is_valid() && step->get_step_uuid() == p_step_uuid) {
			return step;
		}
	}
	return Ref<SequenceAction>();
}

void SequenceData::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_use_first_as_entry", "enabled"), &SequenceData::set_use_first_as_entry);
	ClassDB::bind_method(D_METHOD("is_using_first_as_entry"), &SequenceData::is_using_first_as_entry);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_first_as_entry", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_use_first_as_entry", "is_using_first_as_entry");
	ClassDB::bind_method(D_METHOD("set_sequence_id", "sequence_id"), &SequenceData::set_sequence_id);
	ClassDB::bind_method(D_METHOD("get_sequence_id"), &SequenceData::get_sequence_id);
	ClassDB::bind_method(D_METHOD("set_entry_step_uuid", "entry_step_uuid"), &SequenceData::set_entry_step_uuid);
	ClassDB::bind_method(D_METHOD("get_entry_step_uuid"), &SequenceData::get_entry_step_uuid);
	ClassDB::bind_method(D_METHOD("set_steps", "steps"), &SequenceData::set_steps);
	ClassDB::bind_method(D_METHOD("get_steps"), &SequenceData::get_steps);
	ClassDB::bind_method(D_METHOD("set_editor_graph_meta", "meta"), &SequenceData::set_editor_graph_meta);
	ClassDB::bind_method(D_METHOD("get_editor_graph_meta"), &SequenceData::get_editor_graph_meta);
	ClassDB::bind_method(D_METHOD("find_step", "step_uuid"), &SequenceData::find_step);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "sequence_id"), "set_sequence_id", "get_sequence_id");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "entry_step_uuid"), "set_entry_step_uuid", "get_entry_step_uuid");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "steps", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("SequenceAction")), "set_steps", "get_steps");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "_editor_graph_meta", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_editor_graph_meta", "get_editor_graph_meta");
}
