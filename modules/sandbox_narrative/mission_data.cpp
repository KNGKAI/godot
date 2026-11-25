#include "mission_data.h"

#include "core/object/class_db.h"

void ObjectiveData::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_objective_id", "objective_id"), &ObjectiveData::set_objective_id);
	ClassDB::bind_method(D_METHOD("get_objective_id"), &ObjectiveData::get_objective_id);
	ClassDB::bind_method(D_METHOD("set_title", "title"), &ObjectiveData::set_title);
	ClassDB::bind_method(D_METHOD("get_title"), &ObjectiveData::get_title);
	ClassDB::bind_method(D_METHOD("set_description", "description"), &ObjectiveData::set_description);
	ClassDB::bind_method(D_METHOD("get_description"), &ObjectiveData::get_description);
	ClassDB::bind_method(D_METHOD("set_required", "required"), &ObjectiveData::set_required);
	ClassDB::bind_method(D_METHOD("is_required"), &ObjectiveData::is_required);
	ClassDB::bind_method(D_METHOD("set_complete_condition", "condition"), &ObjectiveData::set_complete_condition);
	ClassDB::bind_method(D_METHOD("get_complete_condition"), &ObjectiveData::get_complete_condition);
	ClassDB::bind_method(D_METHOD("set_fail_condition", "condition"), &ObjectiveData::set_fail_condition);
	ClassDB::bind_method(D_METHOD("get_fail_condition"), &ObjectiveData::get_fail_condition);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "objective_id"), "set_objective_id", "get_objective_id");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "title"), "set_title", "get_title");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "description", PROPERTY_HINT_MULTILINE_TEXT), "set_description", "get_description");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "required"), "set_required", "is_required");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "complete_condition", PROPERTY_HINT_RESOURCE_TYPE, "SandboxCondition"), "set_complete_condition", "get_complete_condition");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "fail_condition", PROPERTY_HINT_RESOURCE_TYPE, "SandboxCondition"), "set_fail_condition", "get_fail_condition");
}

void MissionTransition::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_from_objective_id", "from_objective_id"), &MissionTransition::set_from_objective_id);
	ClassDB::bind_method(D_METHOD("get_from_objective_id"), &MissionTransition::get_from_objective_id);
	ClassDB::bind_method(D_METHOD("set_to_objective_id", "to_objective_id"), &MissionTransition::set_to_objective_id);
	ClassDB::bind_method(D_METHOD("get_to_objective_id"), &MissionTransition::get_to_objective_id);
	ClassDB::bind_method(D_METHOD("set_on_fail", "on_fail"), &MissionTransition::set_on_fail);
	ClassDB::bind_method(D_METHOD("is_on_fail"), &MissionTransition::is_on_fail);
	ClassDB::bind_method(D_METHOD("set_condition", "condition"), &MissionTransition::set_condition);
	ClassDB::bind_method(D_METHOD("get_condition"), &MissionTransition::get_condition);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "from_objective_id"), "set_from_objective_id", "get_from_objective_id");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "to_objective_id"), "set_to_objective_id", "get_to_objective_id");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "on_fail"), "set_on_fail", "is_on_fail");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "condition", PROPERTY_HINT_RESOURCE_TYPE, "SandboxCondition"), "set_condition", "get_condition");
}

Ref<ObjectiveData> MissionData::find_objective(const StringName &p_objective_id) const {
	for (int i = 0; i < objectives.size(); i++) {
		Ref<ObjectiveData> obj = objectives[i];
		if (obj.is_valid() && obj->get_objective_id() == p_objective_id) {
			return obj;
		}
	}
	return Ref<ObjectiveData>();
}

void MissionData::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_use_first_as_entry", "enabled"), &MissionData::set_use_first_as_entry);
	ClassDB::bind_method(D_METHOD("is_using_first_as_entry"), &MissionData::is_using_first_as_entry);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_first_as_entry", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_use_first_as_entry", "is_using_first_as_entry");
	ClassDB::bind_method(D_METHOD("set_prerequisite", "condition"), &MissionData::set_prerequisite);
	ClassDB::bind_method(D_METHOD("get_prerequisite"), &MissionData::get_prerequisite);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "prerequisite", PROPERTY_HINT_RESOURCE_TYPE, "SandboxCondition"), "set_prerequisite", "get_prerequisite");
	ClassDB::bind_method(D_METHOD("set_mission_id", "mission_id"), &MissionData::set_mission_id);
	ClassDB::bind_method(D_METHOD("get_mission_id"), &MissionData::get_mission_id);
	ClassDB::bind_method(D_METHOD("set_entry_objective_id", "entry_objective_id"), &MissionData::set_entry_objective_id);
	ClassDB::bind_method(D_METHOD("get_entry_objective_id"), &MissionData::get_entry_objective_id);
	ClassDB::bind_method(D_METHOD("set_objectives", "objectives"), &MissionData::set_objectives);
	ClassDB::bind_method(D_METHOD("get_objectives"), &MissionData::get_objectives);
	ClassDB::bind_method(D_METHOD("set_transitions", "transitions"), &MissionData::set_transitions);
	ClassDB::bind_method(D_METHOD("get_transitions"), &MissionData::get_transitions);
	ClassDB::bind_method(D_METHOD("set_on_start_sequence", "sequence"), &MissionData::set_on_start_sequence);
	ClassDB::bind_method(D_METHOD("get_on_start_sequence"), &MissionData::get_on_start_sequence);
	ClassDB::bind_method(D_METHOD("set_on_complete_sequence", "sequence"), &MissionData::set_on_complete_sequence);
	ClassDB::bind_method(D_METHOD("get_on_complete_sequence"), &MissionData::get_on_complete_sequence);
	ClassDB::bind_method(D_METHOD("set_editor_graph_meta", "meta"), &MissionData::set_editor_graph_meta);
	ClassDB::bind_method(D_METHOD("get_editor_graph_meta"), &MissionData::get_editor_graph_meta);
	ClassDB::bind_method(D_METHOD("find_objective", "objective_id"), &MissionData::find_objective);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "mission_id", PROPERTY_HINT_NONE, "sandbox_mission_id"), "set_mission_id", "get_mission_id");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "entry_objective_id"), "set_entry_objective_id", "get_entry_objective_id");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "objectives", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("ObjectiveData")), "set_objectives", "get_objectives");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "transitions", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("MissionTransition")), "set_transitions", "get_transitions");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "on_start_sequence", PROPERTY_HINT_RESOURCE_TYPE, "SequenceData"), "set_on_start_sequence", "get_on_start_sequence");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "on_complete_sequence", PROPERTY_HINT_RESOURCE_TYPE, "SequenceData"), "set_on_complete_sequence", "get_on_complete_sequence");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "_editor_graph_meta", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_editor_graph_meta", "get_editor_graph_meta");
}
