#pragma once

#include "core/math/transform_3d.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"
#include "scene/main/node.h"

class SandboxActor : public Node {
	GDCLASS(SandboxActor, Node);

private:
	StringName actor_id;
	TypedArray<StringName> tags;
	bool control_locked = false;
	String control_lock_reason;
	Transform3D last_teleport_transform;
	StringName last_action_id;
	Dictionary last_action_payload;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_actor_id(const StringName &p_actor_id);
	StringName get_actor_id() const { return actor_id; }

	void set_tags(const TypedArray<StringName> &p_tags);
	TypedArray<StringName> get_tags() const { return tags; }
	bool has_tag(const StringName &p_tag) const;

	void request_control_lock(bool p_locked, const String &p_reason = String());
	void request_teleport(const Transform3D &p_target_transform);
	void request_action(const StringName &p_action_id, const Dictionary &p_payload = Dictionary());

	bool is_control_locked() const { return control_locked; }
	String get_control_lock_reason() const { return control_lock_reason; }
	Transform3D get_last_teleport_transform() const { return last_teleport_transform; }
	StringName get_last_action_id() const { return last_action_id; }
	Dictionary get_last_action_payload() const { return last_action_payload; }

	SandboxActor() = default;
	~SandboxActor() override;
};
