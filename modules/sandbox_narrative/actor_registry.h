#pragma once

#include "core/math/transform_3d.h"
#include "core/object/object.h"
#include "core/templates/hash_map.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"

class SandboxActor;

class ActorRegistry : public Object {
	GDCLASS(ActorRegistry, Object);

private:
	static ActorRegistry *singleton;
	HashMap<StringName, ObjectID> actors_by_id;

protected:
	static void _bind_methods();

public:
	static ActorRegistry *get_singleton() { return singleton; }

	void register_actor(SandboxActor *p_actor);
	void unregister_actor(SandboxActor *p_actor);
	void clear();

	SandboxActor *get_actor(const StringName &p_actor_id) const;
	bool has_actor(const StringName &p_actor_id) const;
	TypedArray<SandboxActor> get_actors_by_tag(const StringName &p_tag) const;
	PackedStringArray get_registered_actor_ids() const;

	bool send_control_lock(const StringName &p_actor_id, bool p_locked, const String &p_reason = String());
	bool send_teleport(const StringName &p_actor_id, const Transform3D &p_target_transform);
	bool send_action(const StringName &p_actor_id, const StringName &p_action_id, const Dictionary &p_payload = Dictionary());

	ActorRegistry();
	~ActorRegistry() override;
};
