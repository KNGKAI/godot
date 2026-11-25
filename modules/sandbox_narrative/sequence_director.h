#pragma once

#include "core/templates/hash_set.h"
#include "core/templates/local_vector.h"
#include "scene/main/node.h"

#include "modules/sandbox_narrative/sequence_data.h"

class SequenceDirector : public Node {
	GDCLASS(SequenceDirector, Node);

private:
	struct ActiveStep {
		Ref<SequenceAction> action;
		double remaining_time = 0.0;
		bool waiting_for_event = false;
		StringName expected_event_id;
		bool has_timeout = false;
		PackedStringArray deferred_joins;
	};

	Ref<SequenceData> current_sequence;
	LocalVector<ActiveStep> active_steps;
	HashSet<StringName> completed_steps;
	HashMap<StringName, HashSet<StringName>> join_inputs;
	HashSet<StringName> waiting_joins;
	bool playing = false;
	uint32_t dispatch_depth = 0;

	void _start_step(const StringName &p_step_uuid, const StringName &p_predecessor = StringName());
	void _end_dispatch();
	void _complete_active_step(const ActiveStep &p_step, const StringName &p_event_id = StringName(), const Dictionary &p_payload = Dictionary());
	void _send_trace(const StringName &p_step_uuid, const String &p_state) const;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void play_sequence(const Ref<SequenceData> &p_sequence);
	void stop();
	void step(double p_delta);
	void notify_event(const StringName &p_event_id, const Dictionary &p_payload = Dictionary());

	bool is_playing() const { return playing; }
	Ref<SequenceData> get_current_sequence() const { return current_sequence; }
	PackedStringArray get_active_step_uuids() const;
	bool is_step_completed(const StringName &p_step_uuid) const { return completed_steps.has(p_step_uuid); }

	SequenceDirector() = default;
	~SequenceDirector() override = default;
};
