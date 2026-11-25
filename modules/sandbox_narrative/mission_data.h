#pragma once

#include "core/io/resource.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"

#include "modules/sandbox_narrative/sandbox_condition.h"
#include "modules/sandbox_narrative/sequence_data.h"

class ObjectiveData : public Resource {
	GDCLASS(ObjectiveData, Resource);

private:
	StringName objective_id;
	String title;
	String description;
	bool required = true;
	Ref<SandboxCondition> complete_condition;
	Ref<SandboxCondition> fail_condition;

protected:
	static void _bind_methods();

public:
	void set_objective_id(const StringName &p_id) {
		objective_id = p_id;
		emit_changed();
	}
	StringName get_objective_id() const { return objective_id; }

	void set_title(const String &p_title) {
		title = p_title;
		emit_changed();
	}
	String get_title() const { return title; }

	void set_description(const String &p_desc) {
		description = p_desc;
		emit_changed();
	}
	String get_description() const { return description; }

	void set_required(bool p_req) {
		required = p_req;
		emit_changed();
	}
	bool is_required() const { return required; }

	void set_complete_condition(const Ref<SandboxCondition> &p_cond) {
		complete_condition = p_cond;
		emit_changed();
	}
	Ref<SandboxCondition> get_complete_condition() const { return complete_condition; }

	void set_fail_condition(const Ref<SandboxCondition> &p_cond) {
		fail_condition = p_cond;
		emit_changed();
	}
	Ref<SandboxCondition> get_fail_condition() const { return fail_condition; }
};

class MissionTransition : public Resource {
	GDCLASS(MissionTransition, Resource);

private:
	StringName from_objective_id;
	StringName to_objective_id;
	bool on_fail = false;
	Ref<SandboxCondition> condition;

protected:
	static void _bind_methods();

public:
	void set_from_objective_id(const StringName &p_id) {
		from_objective_id = p_id;
		emit_changed();
	}
	StringName get_from_objective_id() const { return from_objective_id; }

	void set_to_objective_id(const StringName &p_id) {
		to_objective_id = p_id;
		emit_changed();
	}
	StringName get_to_objective_id() const { return to_objective_id; }

	void set_on_fail(bool p_on_fail) {
		on_fail = p_on_fail;
		emit_changed();
	}
	bool is_on_fail() const { return on_fail; }

	void set_condition(const Ref<SandboxCondition> &p_cond) {
		condition = p_cond;
		emit_changed();
	}
	Ref<SandboxCondition> get_condition() const { return condition; }
};

class MissionData : public Resource {
	GDCLASS(MissionData, Resource);

private:
	StringName mission_id;
	Ref<SandboxCondition> prerequisite;
	StringName entry_objective_id;
	bool use_first_as_entry = true;
	TypedArray<ObjectiveData> objectives;
	TypedArray<MissionTransition> transitions;
	Ref<SequenceData> on_start_sequence;
	Ref<SequenceData> on_complete_sequence;
	Dictionary _editor_graph_meta;

protected:
	static void _bind_methods();

public:
	void set_mission_id(const StringName &p_id) {
		mission_id = p_id;
		emit_changed();
	}
	StringName get_mission_id() const { return mission_id; }

	void set_prerequisite(const Ref<SandboxCondition> &p_condition) {
		prerequisite = p_condition;
		emit_changed();
	}
	Ref<SandboxCondition> get_prerequisite() const { return prerequisite; }

	void set_entry_objective_id(const StringName &p_id) {
		entry_objective_id = p_id;
		emit_changed();
	}
	StringName get_entry_objective_id() const { return entry_objective_id; }

	void set_use_first_as_entry(bool p_enabled) {
		use_first_as_entry = p_enabled;
		emit_changed();
	}
	bool is_using_first_as_entry() const { return use_first_as_entry; }

	void set_objectives(const TypedArray<ObjectiveData> &p_objectives) {
		objectives = p_objectives;
		emit_changed();
	}
	TypedArray<ObjectiveData> get_objectives() const { return objectives; }

	void set_transitions(const TypedArray<MissionTransition> &p_transitions) {
		transitions = p_transitions;
		emit_changed();
	}
	TypedArray<MissionTransition> get_transitions() const { return transitions; }

	void set_on_start_sequence(const Ref<SequenceData> &p_seq) {
		on_start_sequence = p_seq;
		emit_changed();
	}
	Ref<SequenceData> get_on_start_sequence() const { return on_start_sequence; }

	void set_on_complete_sequence(const Ref<SequenceData> &p_seq) {
		on_complete_sequence = p_seq;
		emit_changed();
	}
	Ref<SequenceData> get_on_complete_sequence() const { return on_complete_sequence; }

	void set_editor_graph_meta(const Dictionary &p_meta) {
		_editor_graph_meta = p_meta;
		emit_changed();
	}
	Dictionary get_editor_graph_meta() const { return _editor_graph_meta; }

	Ref<ObjectiveData> find_objective(const StringName &p_objective_id) const;
};
