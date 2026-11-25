#include "sandbox_condition.h"

#include "core/object/class_db.h"
#include "modules/sandbox_core/world_state.h"

bool SandboxCondition::evaluate(WorldState *p_state, const StringName &p_event_id, const Dictionary &p_payload) const {
	WorldState *state = p_state ? p_state : WorldState::get_singleton();
	bool ret = false;
	if (GDVIRTUAL_CALL(_evaluate, state, p_event_id, p_payload, ret)) {
		return ret;
	}
	return false;
}

bool SandboxCondition::_evaluate_bind(Object *p_state, const StringName &p_event_id, const Dictionary &p_payload) const {
	WorldState *state = Object::cast_to<WorldState>(p_state);
	ERR_FAIL_COND_V_MSG(p_state && !state, false, "Expected a WorldState for the state argument.");
	return evaluate(state, p_event_id, p_payload);
}

void SandboxCondition::_bind_methods() {
	ClassDB::bind_method(D_METHOD("evaluate", "state", "event_id", "payload"), &SandboxCondition::_evaluate_bind, DEFVAL(StringName()), DEFVAL(Dictionary()));
	GDVIRTUAL_BIND(_evaluate, "state", "event_id", "payload");
}

bool CondStateEquals::evaluate(WorldState *p_state, const StringName &p_event_id, const Dictionary &p_payload) const {
	WorldState *state = p_state ? p_state : WorldState::get_singleton();
	if (!state || key == StringName()) {
		return false;
	}
	Variant current = state->get_value(key, Variant());
	if ((current.get_type() == Variant::INT || current.get_type() == Variant::FLOAT) &&
			(expected.get_type() == Variant::INT || expected.get_type() == Variant::FLOAT)) {
		return Math::is_equal_approx((double)current, (double)expected);
	}
	return current == expected;
}

void CondStateEquals::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_key", "key"), &CondStateEquals::set_key);
	ClassDB::bind_method(D_METHOD("get_key"), &CondStateEquals::get_key);
	ClassDB::bind_method(D_METHOD("set_expected", "expected"), &CondStateEquals::set_expected);
	ClassDB::bind_method(D_METHOD("get_expected"), &CondStateEquals::get_expected);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "key", PROPERTY_HINT_NONE, "sandbox_world_state_key"), "set_key", "get_key");
	ADD_PROPERTY(PropertyInfo(Variant::NIL, "expected", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT), "set_expected", "get_expected");
}

bool CondStateRange::evaluate(WorldState *p_state, const StringName &p_event_id, const Dictionary &p_payload) const {
	WorldState *state = p_state ? p_state : WorldState::get_singleton();
	if (!state || key == StringName() || !state->has_key(key)) {
		return false;
	}
	Variant current = state->get_value(key, Variant());
	if (current.get_type() != Variant::INT && current.get_type() != Variant::FLOAT) {
		return false;
	}
	double v = (double)current;
	return v >= min_val && v <= max_val;
}

void CondStateRange::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_key", "key"), &CondStateRange::set_key);
	ClassDB::bind_method(D_METHOD("get_key"), &CondStateRange::get_key);
	ClassDB::bind_method(D_METHOD("set_min_val", "min_val"), &CondStateRange::set_min_val);
	ClassDB::bind_method(D_METHOD("get_min_val"), &CondStateRange::get_min_val);
	ClassDB::bind_method(D_METHOD("set_max_val", "max_val"), &CondStateRange::set_max_val);
	ClassDB::bind_method(D_METHOD("get_max_val"), &CondStateRange::get_max_val);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "key", PROPERTY_HINT_NONE, "sandbox_world_state_key:float"), "set_key", "get_key");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_val"), "set_min_val", "get_min_val");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_val"), "set_max_val", "get_max_val");
}

bool CondEventFired::evaluate(WorldState *p_state, const StringName &p_event_id, const Dictionary &p_payload) const {
	if (expected_event_id == StringName() || p_event_id != expected_event_id) {
		return false;
	}
	LocalVector<Variant> match_keys = payload_match.get_key_list();
	for (const Variant &k : match_keys) {
		if (!p_payload.has(k) || p_payload[k] != payload_match[k]) {
			return false;
		}
	}
	return true;
}

void CondEventFired::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_expected_event_id", "expected_event_id"), &CondEventFired::set_expected_event_id);
	ClassDB::bind_method(D_METHOD("get_expected_event_id"), &CondEventFired::get_expected_event_id);
	ClassDB::bind_method(D_METHOD("set_payload_match", "payload_match"), &CondEventFired::set_payload_match);
	ClassDB::bind_method(D_METHOD("get_payload_match"), &CondEventFired::get_payload_match);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "expected_event_id", PROPERTY_HINT_NONE, "sandbox_event_id"), "set_expected_event_id", "get_expected_event_id");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "payload_match"), "set_payload_match", "get_payload_match");
}

bool CondAll::evaluate(WorldState *p_state, const StringName &p_event_id, const Dictionary &p_payload) const {
	if (children.is_empty()) {
		return false;
	}
	for (int i = 0; i < children.size(); i++) {
		Ref<SandboxCondition> cond = children[i];
		if (cond.is_null() || !cond->evaluate(p_state, p_event_id, p_payload)) {
			return false;
		}
	}
	return true;
}

void CondAll::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_children", "children"), &CondAll::set_children);
	ClassDB::bind_method(D_METHOD("get_children"), &CondAll::get_children);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "children", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("SandboxCondition")), "set_children", "get_children");
}

bool CondAny::evaluate(WorldState *p_state, const StringName &p_event_id, const Dictionary &p_payload) const {
	for (int i = 0; i < children.size(); i++) {
		Ref<SandboxCondition> cond = children[i];
		if (cond.is_valid() && cond->evaluate(p_state, p_event_id, p_payload)) {
			return true;
		}
	}
	return false;
}

void CondAny::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_children", "children"), &CondAny::set_children);
	ClassDB::bind_method(D_METHOD("get_children"), &CondAny::get_children);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "children", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("SandboxCondition")), "set_children", "get_children");
}

bool CondNot::evaluate(WorldState *p_state, const StringName &p_event_id, const Dictionary &p_payload) const {
	if (child.is_null()) {
		return true;
	}
	return !child->evaluate(p_state, p_event_id, p_payload);
}

void CondNot::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_child", "child"), &CondNot::set_child);
	ClassDB::bind_method(D_METHOD("get_child"), &CondNot::get_child);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "child", PROPERTY_HINT_RESOURCE_TYPE, "SandboxCondition"), "set_child", "get_child");
}
