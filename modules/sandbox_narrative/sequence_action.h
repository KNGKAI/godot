#pragma once

#include "core/io/resource.h"
#include "core/object/gdvirtual.gen.h"
#include "core/variant/dictionary.h"

#include "modules/sandbox_narrative/sandbox_condition.h"

class EventBus;
class SequenceDirector;
class WorldState;

class SequenceAction : public Resource {
	GDCLASS(SequenceAction, Resource);

private:
	StringName step_uuid;
	bool wait_for_completion = true;
	double duration_sec = 0.0;
	PackedStringArray next_steps;

protected:
	static void _bind_methods();

	GDVIRTUAL1(_execute, Object *)

public:
	void set_step_uuid(const StringName &p_uuid) {
		step_uuid = p_uuid;
		emit_changed();
	}
	StringName get_step_uuid() const { return step_uuid; }

	void set_wait_for_completion(bool p_wait) {
		wait_for_completion = p_wait;
		emit_changed();
	}
	bool get_wait_for_completion() const { return wait_for_completion; }

	void set_duration_sec(double p_duration) {
		duration_sec = MAX(0.0, p_duration);
		emit_changed();
	}
	double get_duration_sec() const { return duration_sec; }

	void set_next_steps(const PackedStringArray &p_next) {
		next_steps = p_next;
		emit_changed();
	}
	PackedStringArray get_next_steps() const { return next_steps; }

	virtual void execute(SequenceDirector *p_director, WorldState *p_state, EventBus *p_bus);
	virtual PackedStringArray resolve_next_steps(WorldState *p_state, const StringName &p_event_id = StringName(), const Dictionary &p_payload = Dictionary()) const;
};

// An explicit barrier distinguishes an all-input join from ordinary converging alternatives.
class ActionJoin : public SequenceAction {
	GDCLASS(ActionJoin, SequenceAction);

protected:
	static void _bind_methods() {}
};

class ActionPlayAnimation : public SequenceAction {
	GDCLASS(ActionPlayAnimation, SequenceAction);

private:
	StringName target_actor_id;
	NodePath animation_player_path;
	StringName animation_name;

protected:
	static void _bind_methods();

public:
	void set_target_actor_id(const StringName &p_id) {
		target_actor_id = p_id;
		emit_changed();
	}
	StringName get_target_actor_id() const { return target_actor_id; }

	void set_animation_player_path(const NodePath &p_path) {
		animation_player_path = p_path;
		emit_changed();
	}
	NodePath get_animation_player_path() const { return animation_player_path; }

	void set_animation_name(const StringName &p_name) {
		animation_name = p_name;
		emit_changed();
	}
	StringName get_animation_name() const { return animation_name; }

	void execute(SequenceDirector *p_director, WorldState *p_state, EventBus *p_bus) override;
};

class ActionSetState : public SequenceAction {
	GDCLASS(ActionSetState, SequenceAction);

private:
	StringName key;
	Variant value;

protected:
	static void _bind_methods();

public:
	void set_key(const StringName &p_key) {
		key = p_key;
		emit_changed();
	}
	StringName get_key() const { return key; }

	void set_value(const Variant &p_val) {
		value = p_val;
		emit_changed();
	}
	Variant get_value() const { return value; }

	void execute(SequenceDirector *p_director, WorldState *p_state, EventBus *p_bus) override;
};

class ActionEmitEvent : public SequenceAction {
	GDCLASS(ActionEmitEvent, SequenceAction);

private:
	StringName event_id;
	Dictionary payload;

protected:
	static void _bind_methods();

public:
	void set_event_id(const StringName &p_id) {
		event_id = p_id;
		emit_changed();
	}
	StringName get_event_id() const { return event_id; }

	void set_payload(const Dictionary &p_payload) {
		payload = p_payload;
		emit_changed();
	}
	Dictionary get_payload() const { return payload; }

	void execute(SequenceDirector *p_director, WorldState *p_state, EventBus *p_bus) override;
};

class ActionRunCommand : public SequenceAction {
	GDCLASS(ActionRunCommand, SequenceAction);

private:
	String command_string;

protected:
	static void _bind_methods();

public:
	void set_command_string(const String &p_cmd) {
		command_string = p_cmd;
		emit_changed();
	}
	String get_command_string() const { return command_string; }

	void execute(SequenceDirector *p_director, WorldState *p_state, EventBus *p_bus) override;
};

class ActionLockActor : public SequenceAction {
	GDCLASS(ActionLockActor, SequenceAction);

private:
	StringName actor_id;
	bool locked = true;
	String reason;

protected:
	static void _bind_methods();

public:
	void set_actor_id(const StringName &p_id) {
		actor_id = p_id;
		emit_changed();
	}
	StringName get_actor_id() const { return actor_id; }

	void set_locked(bool p_locked) {
		locked = p_locked;
		emit_changed();
	}
	bool get_locked() const { return locked; }

	void set_reason(const String &p_reason) {
		reason = p_reason;
		emit_changed();
	}
	String get_reason() const { return reason; }

	void execute(SequenceDirector *p_director, WorldState *p_state, EventBus *p_bus) override;
};

class ActionBranchCondition : public SequenceAction {
	GDCLASS(ActionBranchCondition, SequenceAction);

private:
	Ref<SandboxCondition> condition;
	PackedStringArray true_steps;
	PackedStringArray false_steps;

protected:
	static void _bind_methods();

public:
	void set_condition(const Ref<SandboxCondition> &p_cond) {
		condition = p_cond;
		emit_changed();
	}
	Ref<SandboxCondition> get_condition() const { return condition; }

	void set_true_steps(const PackedStringArray &p_steps) {
		true_steps = p_steps;
		emit_changed();
	}
	PackedStringArray get_true_steps() const { return true_steps; }

	void set_false_steps(const PackedStringArray &p_steps) {
		false_steps = p_steps;
		emit_changed();
	}
	PackedStringArray get_false_steps() const { return false_steps; }

	PackedStringArray resolve_next_steps(WorldState *p_state, const StringName &p_event_id = StringName(), const Dictionary &p_payload = Dictionary()) const override;
};

class ActionWaitEvent : public SequenceAction {
	GDCLASS(ActionWaitEvent, SequenceAction);

private:
	StringName expected_event_id;
	double timeout_sec = 0.0;

protected:
	static void _bind_methods();

public:
	void set_expected_event_id(const StringName &p_id) {
		expected_event_id = p_id;
		emit_changed();
	}
	StringName get_expected_event_id() const { return expected_event_id; }

	void set_timeout_sec(double p_timeout) {
		timeout_sec = MAX(0.0, p_timeout);
		emit_changed();
	}
	double get_timeout_sec() const { return timeout_sec; }
};

class ActionBlendCamera : public SequenceAction {
	GDCLASS(ActionBlendCamera, SequenceAction);

private:
	StringName target_camera_actor_id;
	double blend_duration = 1.0;

protected:
	static void _bind_methods();

public:
	void set_target_camera_actor_id(const StringName &p_id) {
		target_camera_actor_id = p_id;
		emit_changed();
	}
	StringName get_target_camera_actor_id() const { return target_camera_actor_id; }

	void set_blend_duration(double p_duration) {
		blend_duration = MAX(0.0, p_duration);
		emit_changed();
	}
	double get_blend_duration() const { return blend_duration; }

	void execute(SequenceDirector *p_director, WorldState *p_state, EventBus *p_bus) override;
};
