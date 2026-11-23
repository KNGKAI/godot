#include "sequence_action.h"

#include "core/object/class_db.h"
#include "modules/sandbox_core/command_bus.h"
#include "modules/sandbox_core/event_bus.h"
#include "modules/sandbox_core/world_state.h"
#include "modules/sandbox_narrative/actor_registry.h"
#include "modules/sandbox_narrative/sandbox_actor.h"
#include "modules/sandbox_narrative/sequence_director.h"
#include "scene/animation/animation_player.h"
#include "scene/animation/tween.h"

void SequenceAction::execute(SequenceDirector *p_director, WorldState *p_state, EventBus *p_bus) {
	GDVIRTUAL_CALL(_execute, p_director);
}

PackedStringArray SequenceAction::resolve_next_steps(WorldState *p_state, const StringName &p_event_id, const Dictionary &p_payload) const {
	return next_steps;
}

void SequenceAction::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_step_uuid", "step_uuid"), &SequenceAction::set_step_uuid);
	ClassDB::bind_method(D_METHOD("get_step_uuid"), &SequenceAction::get_step_uuid);
	ClassDB::bind_method(D_METHOD("set_wait_for_completion", "wait"), &SequenceAction::set_wait_for_completion);
	ClassDB::bind_method(D_METHOD("get_wait_for_completion"), &SequenceAction::get_wait_for_completion);
	ClassDB::bind_method(D_METHOD("set_duration_sec", "duration_sec"), &SequenceAction::set_duration_sec);
	ClassDB::bind_method(D_METHOD("get_duration_sec"), &SequenceAction::get_duration_sec);
	ClassDB::bind_method(D_METHOD("set_next_steps", "next_steps"), &SequenceAction::set_next_steps);
	ClassDB::bind_method(D_METHOD("get_next_steps"), &SequenceAction::get_next_steps);

	GDVIRTUAL_BIND(_execute, "director");

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "step_uuid"), "set_step_uuid", "get_step_uuid");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "wait_for_completion"), "set_wait_for_completion", "get_wait_for_completion");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "duration_sec"), "set_duration_sec", "get_duration_sec");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "next_steps"), "set_next_steps", "get_next_steps");
}

void ActionPlayAnimation::execute(SequenceDirector *p_director, WorldState *p_state, EventBus *p_bus) {
	if (target_actor_id != StringName() && ActorRegistry::get_singleton()) {
		Dictionary payload;
		payload["animation_name"] = animation_name;
		payload["animation_player_path"] = animation_player_path;
		ActorRegistry::get_singleton()->send_action(target_actor_id, SNAME("play_animation"), payload);

		SandboxActor *actor = ActorRegistry::get_singleton()->get_actor(target_actor_id);
		if (actor && !animation_player_path.is_empty()) {
			AnimationPlayer *player = Object::cast_to<AnimationPlayer>(actor->get_node_or_null(animation_player_path));
			if (player && animation_name != StringName()) {
				player->play(animation_name);
			}
		}
	} else if (p_director && !animation_player_path.is_empty()) {
		AnimationPlayer *player = Object::cast_to<AnimationPlayer>(p_director->get_node_or_null(animation_player_path));
		if (player && animation_name != StringName()) {
			player->play(animation_name);
		}
	}
}

void ActionPlayAnimation::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_target_actor_id", "actor_id"), &ActionPlayAnimation::set_target_actor_id);
	ClassDB::bind_method(D_METHOD("get_target_actor_id"), &ActionPlayAnimation::get_target_actor_id);
	ClassDB::bind_method(D_METHOD("set_animation_player_path", "path"), &ActionPlayAnimation::set_animation_player_path);
	ClassDB::bind_method(D_METHOD("get_animation_player_path"), &ActionPlayAnimation::get_animation_player_path);
	ClassDB::bind_method(D_METHOD("set_animation_name", "name"), &ActionPlayAnimation::set_animation_name);
	ClassDB::bind_method(D_METHOD("get_animation_name"), &ActionPlayAnimation::get_animation_name);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "target_actor_id", PROPERTY_HINT_NONE, "sandbox_actor_id"), "set_target_actor_id", "get_target_actor_id");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "animation_player_path"), "set_animation_player_path", "get_animation_player_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "animation_name"), "set_animation_name", "get_animation_name");
}

void ActionSetState::execute(SequenceDirector *p_director, WorldState *p_state, EventBus *p_bus) {
	WorldState *state = p_state ? p_state : WorldState::get_singleton();
	if (state && key != StringName()) {
		state->set_value(key, value);
	}
}

void ActionSetState::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_key", "key"), &ActionSetState::set_key);
	ClassDB::bind_method(D_METHOD("get_key"), &ActionSetState::get_key);
	ClassDB::bind_method(D_METHOD("set_value", "value"), &ActionSetState::set_value);
	ClassDB::bind_method(D_METHOD("get_value"), &ActionSetState::get_value);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "key", PROPERTY_HINT_NONE, "sandbox_world_state_key"), "set_key", "get_key");
	ADD_PROPERTY(PropertyInfo(Variant::NIL, "value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT), "set_value", "get_value");
}

void ActionEmitEvent::execute(SequenceDirector *p_director, WorldState *p_state, EventBus *p_bus) {
	EventBus *bus = p_bus ? p_bus : EventBus::get_singleton();
	if (bus && event_id != StringName()) {
		bus->emit(event_id, payload);
	}
}

void ActionEmitEvent::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_event_id", "event_id"), &ActionEmitEvent::set_event_id);
	ClassDB::bind_method(D_METHOD("get_event_id"), &ActionEmitEvent::get_event_id);
	ClassDB::bind_method(D_METHOD("set_payload", "payload"), &ActionEmitEvent::set_payload);
	ClassDB::bind_method(D_METHOD("get_payload"), &ActionEmitEvent::get_payload);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "event_id", PROPERTY_HINT_NONE, "sandbox_event_id"), "set_event_id", "get_event_id");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "payload"), "set_payload", "get_payload");
}

void ActionRunCommand::execute(SequenceDirector *p_director, WorldState *p_state, EventBus *p_bus) {
	if (CommandBus::get_singleton() && !command_string.is_empty()) {
		CommandBus::get_singleton()->execute(command_string, CommandBus::PERMISSION_ADMIN);
	}
}

void ActionRunCommand::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_command_string", "command_string"), &ActionRunCommand::set_command_string);
	ClassDB::bind_method(D_METHOD("get_command_string"), &ActionRunCommand::get_command_string);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "command_string"), "set_command_string", "get_command_string");
}

void ActionLockActor::execute(SequenceDirector *p_director, WorldState *p_state, EventBus *p_bus) {
	if (ActorRegistry::get_singleton() && actor_id != StringName()) {
		ActorRegistry::get_singleton()->send_control_lock(actor_id, locked, reason);
	}
}

void ActionLockActor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_actor_id", "actor_id"), &ActionLockActor::set_actor_id);
	ClassDB::bind_method(D_METHOD("get_actor_id"), &ActionLockActor::get_actor_id);
	ClassDB::bind_method(D_METHOD("set_locked", "locked"), &ActionLockActor::set_locked);
	ClassDB::bind_method(D_METHOD("get_locked"), &ActionLockActor::get_locked);
	ClassDB::bind_method(D_METHOD("set_reason", "reason"), &ActionLockActor::set_reason);
	ClassDB::bind_method(D_METHOD("get_reason"), &ActionLockActor::get_reason);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "actor_id", PROPERTY_HINT_NONE, "sandbox_actor_id"), "set_actor_id", "get_actor_id");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "locked"), "set_locked", "get_locked");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "reason"), "set_reason", "get_reason");
}

PackedStringArray ActionBranchCondition::resolve_next_steps(WorldState *p_state, const StringName &p_event_id, const Dictionary &p_payload) const {
	bool passed = false;
	if (condition.is_valid()) {
		passed = condition->evaluate(p_state, p_event_id, p_payload);
	}
	return passed ? true_steps : false_steps;
}

void ActionBranchCondition::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_condition", "condition"), &ActionBranchCondition::set_condition);
	ClassDB::bind_method(D_METHOD("get_condition"), &ActionBranchCondition::get_condition);
	ClassDB::bind_method(D_METHOD("set_true_steps", "true_steps"), &ActionBranchCondition::set_true_steps);
	ClassDB::bind_method(D_METHOD("get_true_steps"), &ActionBranchCondition::get_true_steps);
	ClassDB::bind_method(D_METHOD("set_false_steps", "false_steps"), &ActionBranchCondition::set_false_steps);
	ClassDB::bind_method(D_METHOD("get_false_steps"), &ActionBranchCondition::get_false_steps);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "condition", PROPERTY_HINT_RESOURCE_TYPE, "SandboxCondition"), "set_condition", "get_condition");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "true_steps"), "set_true_steps", "get_true_steps");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "false_steps"), "set_false_steps", "get_false_steps");
}

void ActionWaitEvent::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_expected_event_id", "expected_event_id"), &ActionWaitEvent::set_expected_event_id);
	ClassDB::bind_method(D_METHOD("get_expected_event_id"), &ActionWaitEvent::get_expected_event_id);
	ClassDB::bind_method(D_METHOD("set_timeout_sec", "timeout_sec"), &ActionWaitEvent::set_timeout_sec);
	ClassDB::bind_method(D_METHOD("get_timeout_sec"), &ActionWaitEvent::get_timeout_sec);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "expected_event_id", PROPERTY_HINT_NONE, "sandbox_event_id"), "set_expected_event_id", "get_expected_event_id");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "timeout_sec"), "set_timeout_sec", "get_timeout_sec");
}

void ActionBlendCamera::execute(SequenceDirector *p_director, WorldState *p_state, EventBus *p_bus) {
	if (ActorRegistry::get_singleton() && target_camera_actor_id != StringName()) {
		Dictionary payload;
		payload["blend_duration"] = blend_duration;
		ActorRegistry::get_singleton()->send_action(target_camera_actor_id, SNAME("blend_camera"), payload);
	}
	if (p_director && p_director->is_inside_tree() && blend_duration > 0.0) {
		Ref<Tween> tween = p_director->create_tween();
		if (tween.is_valid()) {
			Ref<IntervalTweener> interval = tween->tween_interval(blend_duration);
			(void)interval;
		}
	}
}

void ActionBlendCamera::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_target_camera_actor_id", "target_camera_actor_id"), &ActionBlendCamera::set_target_camera_actor_id);
	ClassDB::bind_method(D_METHOD("get_target_camera_actor_id"), &ActionBlendCamera::get_target_camera_actor_id);
	ClassDB::bind_method(D_METHOD("set_blend_duration", "blend_duration"), &ActionBlendCamera::set_blend_duration);
	ClassDB::bind_method(D_METHOD("get_blend_duration"), &ActionBlendCamera::get_blend_duration);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "target_camera_actor_id", PROPERTY_HINT_NONE, "sandbox_actor_id"), "set_target_camera_actor_id", "get_target_camera_actor_id");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "blend_duration"), "set_blend_duration", "get_blend_duration");
}
