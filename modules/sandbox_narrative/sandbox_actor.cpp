#include "sandbox_actor.h"

#include "core/object/class_db.h"
#include "modules/sandbox_narrative/actor_registry.h"

void SandboxActor::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (ActorRegistry::get_singleton() && actor_id != StringName()) {
				ActorRegistry::get_singleton()->register_actor(this);
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (ActorRegistry::get_singleton()) {
				ActorRegistry::get_singleton()->unregister_actor(this);
			}
		} break;
	}
}

void SandboxActor::set_actor_id(const StringName &p_actor_id) {
	if (actor_id == p_actor_id) {
		return;
	}
	if (ActorRegistry::get_singleton() && actor_id != StringName()) {
		ActorRegistry::get_singleton()->unregister_actor(this);
	}
	actor_id = p_actor_id;
	if (ActorRegistry::get_singleton() && actor_id != StringName()) {
		ActorRegistry::get_singleton()->register_actor(this);
	}
}

void SandboxActor::set_tags(const TypedArray<StringName> &p_tags) {
	tags = p_tags;
}

bool SandboxActor::has_tag(const StringName &p_tag) const {
	for (int i = 0; i < tags.size(); i++) {
		if (StringName(tags[i]) == p_tag) {
			return true;
		}
	}
	return false;
}

void SandboxActor::request_control_lock(bool p_locked, const String &p_reason) {
	control_locked = p_locked;
	control_lock_reason = p_reason;
	emit_signal(SNAME("control_lock_requested"), p_locked, p_reason);
}

void SandboxActor::request_teleport(const Transform3D &p_target_transform) {
	last_teleport_transform = p_target_transform;
	emit_signal(SNAME("teleport_requested"), p_target_transform);
}

void SandboxActor::request_action(const StringName &p_action_id, const Dictionary &p_payload) {
	last_action_id = p_action_id;
	last_action_payload = p_payload;
	emit_signal(SNAME("action_requested"), p_action_id, p_payload);
}

void SandboxActor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_actor_id", "actor_id"), &SandboxActor::set_actor_id);
	ClassDB::bind_method(D_METHOD("get_actor_id"), &SandboxActor::get_actor_id);
	ClassDB::bind_method(D_METHOD("set_tags", "tags"), &SandboxActor::set_tags);
	ClassDB::bind_method(D_METHOD("get_tags"), &SandboxActor::get_tags);
	ClassDB::bind_method(D_METHOD("has_tag", "tag"), &SandboxActor::has_tag);

	ClassDB::bind_method(D_METHOD("request_control_lock", "locked", "reason"), &SandboxActor::request_control_lock, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("request_teleport", "target_transform"), &SandboxActor::request_teleport);
	ClassDB::bind_method(D_METHOD("request_action", "action_id", "payload"), &SandboxActor::request_action, DEFVAL(Dictionary()));

	ClassDB::bind_method(D_METHOD("is_control_locked"), &SandboxActor::is_control_locked);
	ClassDB::bind_method(D_METHOD("get_control_lock_reason"), &SandboxActor::get_control_lock_reason);
	ClassDB::bind_method(D_METHOD("get_last_teleport_transform"), &SandboxActor::get_last_teleport_transform);
	ClassDB::bind_method(D_METHOD("get_last_action_id"), &SandboxActor::get_last_action_id);
	ClassDB::bind_method(D_METHOD("get_last_action_payload"), &SandboxActor::get_last_action_payload);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "actor_id", PROPERTY_HINT_NONE, "sandbox_actor_id"), "set_actor_id", "get_actor_id");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "tags", PROPERTY_HINT_ARRAY_TYPE, "StringName"), "set_tags", "get_tags");

	ADD_SIGNAL(MethodInfo("control_lock_requested", PropertyInfo(Variant::BOOL, "locked"), PropertyInfo(Variant::STRING, "reason")));
	ADD_SIGNAL(MethodInfo("teleport_requested", PropertyInfo(Variant::TRANSFORM3D, "target_transform")));
	ADD_SIGNAL(MethodInfo("action_requested", PropertyInfo(Variant::STRING_NAME, "action_id"), PropertyInfo(Variant::DICTIONARY, "payload")));
}

SandboxActor::~SandboxActor() {
	if (ActorRegistry::get_singleton()) {
		ActorRegistry::get_singleton()->unregister_actor(this);
	}
}
