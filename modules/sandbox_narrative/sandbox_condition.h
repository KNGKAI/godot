#pragma once

#include "core/io/resource.h"
#include "core/object/gdvirtual.gen.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"

class WorldState;

class SandboxCondition : public Resource {
	GDCLASS(SandboxCondition, Resource);

	bool _evaluate_bind(Object *p_state, const StringName &p_event_id, const Dictionary &p_payload) const;

protected:
	static void _bind_methods();

	GDVIRTUAL3RC(bool, _evaluate, Object *, StringName, Dictionary)

public:
	virtual bool evaluate(WorldState *p_state, const StringName &p_event_id = StringName(), const Dictionary &p_payload = Dictionary()) const;
};

class CondStateEquals : public SandboxCondition {
	GDCLASS(CondStateEquals, SandboxCondition);

private:
	StringName key;
	Variant expected;

protected:
	static void _bind_methods();

public:
	void set_key(const StringName &p_key) {
		key = p_key;
		emit_changed();
	}
	StringName get_key() const { return key; }

	void set_expected(const Variant &p_expected) {
		expected = p_expected;
		emit_changed();
	}
	Variant get_expected() const { return expected; }

	bool evaluate(WorldState *p_state, const StringName &p_event_id = StringName(), const Dictionary &p_payload = Dictionary()) const override;
};

class CondStateRange : public SandboxCondition {
	GDCLASS(CondStateRange, SandboxCondition);

private:
	StringName key;
	double min_val = 0.0;
	double max_val = 1.0;

protected:
	static void _bind_methods();

public:
	void set_key(const StringName &p_key) {
		key = p_key;
		emit_changed();
	}
	StringName get_key() const { return key; }

	void set_min_val(double p_min) {
		min_val = p_min;
		emit_changed();
	}
	double get_min_val() const { return min_val; }

	void set_max_val(double p_max) {
		max_val = p_max;
		emit_changed();
	}
	double get_max_val() const { return max_val; }

	bool evaluate(WorldState *p_state, const StringName &p_event_id = StringName(), const Dictionary &p_payload = Dictionary()) const override;
};

class CondEventFired : public SandboxCondition {
	GDCLASS(CondEventFired, SandboxCondition);

private:
	StringName expected_event_id;
	Dictionary payload_match;

protected:
	static void _bind_methods();

public:
	void set_expected_event_id(const StringName &p_id) {
		expected_event_id = p_id;
		emit_changed();
	}
	StringName get_expected_event_id() const { return expected_event_id; }

	void set_payload_match(const Dictionary &p_match) {
		payload_match = p_match;
		emit_changed();
	}
	Dictionary get_payload_match() const { return payload_match; }

	bool evaluate(WorldState *p_state, const StringName &p_event_id = StringName(), const Dictionary &p_payload = Dictionary()) const override;
};

class CondAll : public SandboxCondition {
	GDCLASS(CondAll, SandboxCondition);

private:
	TypedArray<SandboxCondition> children;

protected:
	static void _bind_methods();

public:
	void set_children(const TypedArray<SandboxCondition> &p_children) {
		children = p_children;
		emit_changed();
	}
	TypedArray<SandboxCondition> get_children() const { return children; }

	bool evaluate(WorldState *p_state, const StringName &p_event_id = StringName(), const Dictionary &p_payload = Dictionary()) const override;
};

class CondAny : public SandboxCondition {
	GDCLASS(CondAny, SandboxCondition);

private:
	TypedArray<SandboxCondition> children;

protected:
	static void _bind_methods();

public:
	void set_children(const TypedArray<SandboxCondition> &p_children) {
		children = p_children;
		emit_changed();
	}
	TypedArray<SandboxCondition> get_children() const { return children; }

	bool evaluate(WorldState *p_state, const StringName &p_event_id = StringName(), const Dictionary &p_payload = Dictionary()) const override;
};

class CondNot : public SandboxCondition {
	GDCLASS(CondNot, SandboxCondition);

private:
	Ref<SandboxCondition> child;

protected:
	static void _bind_methods();

public:
	void set_child(const Ref<SandboxCondition> &p_child) {
		child = p_child;
		emit_changed();
	}
	Ref<SandboxCondition> get_child() const { return child; }

	bool evaluate(WorldState *p_state, const StringName &p_event_id = StringName(), const Dictionary &p_payload = Dictionary()) const override;
};
