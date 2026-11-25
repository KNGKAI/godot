#include "actor_registry.h"

#include "core/object/class_db.h"
#include "modules/sandbox_narrative/sandbox_actor.h"

ActorRegistry *ActorRegistry::singleton = nullptr;

void ActorRegistry::register_actor(SandboxActor *p_actor) {
	ERR_FAIL_NULL(p_actor);
	StringName id = p_actor->get_actor_id();
	if (id == StringName()) {
		return;
	}
	actors_by_id[id] = p_actor->get_instance_id();
}

void ActorRegistry::unregister_actor(SandboxActor *p_actor) {
	if (!p_actor) {
		return;
	}
	StringName id = p_actor->get_actor_id();
	if (id != StringName()) {
		HashMap<StringName, ObjectID>::Iterator it = actors_by_id.find(id);
		if (it != actors_by_id.end() && it->value == p_actor->get_instance_id()) {
			actors_by_id.remove(it);
		}
	}
	for (HashMap<StringName, ObjectID>::Iterator it = actors_by_id.begin(); it != actors_by_id.end();) {
		HashMap<StringName, ObjectID>::Iterator current = it;
		++it;
		if (current->value == p_actor->get_instance_id()) {
			actors_by_id.remove(current);
		}
	}
}

void ActorRegistry::clear() {
	actors_by_id.clear();
}

SandboxActor *ActorRegistry::get_actor(const StringName &p_actor_id) const {
	HashMap<StringName, ObjectID>::ConstIterator it = actors_by_id.find(p_actor_id);
	if (it == actors_by_id.end()) {
		return nullptr;
	}
	return Object::cast_to<SandboxActor>(ObjectDB::get_instance(it->value));
}

bool ActorRegistry::has_actor(const StringName &p_actor_id) const {
	return get_actor(p_actor_id) != nullptr;
}

TypedArray<SandboxActor> ActorRegistry::get_actors_by_tag(const StringName &p_tag) const {
	TypedArray<SandboxActor> matches;
	for (const KeyValue<StringName, ObjectID> &E : actors_by_id) {
		SandboxActor *actor = Object::cast_to<SandboxActor>(ObjectDB::get_instance(E.value));
		if (actor && actor->has_tag(p_tag)) {
			matches.push_back(actor);
		}
	}
	return matches;
}

PackedStringArray ActorRegistry::get_registered_actor_ids() const {
	PackedStringArray ids;
	for (const KeyValue<StringName, ObjectID> &E : actors_by_id) {
		if (ObjectDB::get_instance(E.value) != nullptr) {
			ids.push_back(String(E.key));
		}
	}
	return ids;
}

bool ActorRegistry::send_control_lock(const StringName &p_actor_id, bool p_locked, const String &p_reason) {
	SandboxActor *actor = get_actor(p_actor_id);
	if (!actor) {
		return false;
	}
	actor->request_control_lock(p_locked, p_reason);
	return true;
}

bool ActorRegistry::send_teleport(const StringName &p_actor_id, const Transform3D &p_target_transform) {
	SandboxActor *actor = get_actor(p_actor_id);
	if (!actor) {
		return false;
	}
	actor->request_teleport(p_target_transform);
	return true;
}

bool ActorRegistry::send_action(const StringName &p_actor_id, const StringName &p_action_id, const Dictionary &p_payload) {
	SandboxActor *actor = get_actor(p_actor_id);
	if (!actor) {
		return false;
	}
	actor->request_action(p_action_id, p_payload);
	return true;
}

void ActorRegistry::_bind_methods() {
	ClassDB::bind_method(D_METHOD("register_actor", "actor"), &ActorRegistry::register_actor);
	ClassDB::bind_method(D_METHOD("unregister_actor", "actor"), &ActorRegistry::unregister_actor);
	ClassDB::bind_method(D_METHOD("clear"), &ActorRegistry::clear);
	ClassDB::bind_method(D_METHOD("get_actor", "actor_id"), &ActorRegistry::get_actor);
	ClassDB::bind_method(D_METHOD("has_actor", "actor_id"), &ActorRegistry::has_actor);
	ClassDB::bind_method(D_METHOD("get_actors_by_tag", "tag"), &ActorRegistry::get_actors_by_tag);
	ClassDB::bind_method(D_METHOD("get_registered_actor_ids"), &ActorRegistry::get_registered_actor_ids);

	ClassDB::bind_method(D_METHOD("send_control_lock", "actor_id", "locked", "reason"), &ActorRegistry::send_control_lock, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("send_teleport", "actor_id", "target_transform"), &ActorRegistry::send_teleport);
	ClassDB::bind_method(D_METHOD("send_action", "actor_id", "action_id", "payload"), &ActorRegistry::send_action, DEFVAL(Dictionary()));
}

ActorRegistry::ActorRegistry() {
	singleton = this;
}

ActorRegistry::~ActorRegistry() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
