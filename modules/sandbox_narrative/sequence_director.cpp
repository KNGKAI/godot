#include "sequence_director.h"

#include "core/debugger/engine_debugger.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"

#include "modules/sandbox_core/event_bus.h"
#include "modules/sandbox_core/sandbox_clock.h"
#include "modules/sandbox_core/world_state.h"

void SequenceDirector::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE:
			EventBus::get_singleton()->connect(SNAME("event_emitted"), callable_mp(this, &SequenceDirector::notify_event));
			break;
		case NOTIFICATION_EXIT_TREE:
			EventBus::get_singleton()->disconnect(SNAME("event_emitted"), callable_mp(this, &SequenceDirector::notify_event));
			break;
		case NOTIFICATION_PROCESS:
			if (playing) {
				step(SandboxClock::get_singleton()->get_narrative_delta());
			}
			break;
	}
}

void SequenceDirector::_send_trace(const StringName &p_step_uuid, const String &p_state) const {
	if (EngineDebugger::is_active() && current_sequence.is_valid()) {
		Array trace_data;
		trace_data.push_back("sequence");
		trace_data.push_back(String(current_sequence->get_sequence_id()));
		trace_data.push_back(String(p_step_uuid));
		trace_data.push_back(p_state);
		EngineDebugger::get_singleton()->send_message("sandbox:narrative_trace", trace_data);
	}
}

void SequenceDirector::play_sequence(const Ref<SequenceData> &p_sequence) {
	ERR_FAIL_COND(p_sequence.is_null());
	current_sequence = p_sequence;
	active_steps.clear();
	completed_steps.clear();
	join_inputs.clear();
	waiting_joins.clear();
	const TypedArray<SequenceAction> actions = p_sequence->get_steps();
	for (int i = 0; i < actions.size(); i++) {
		Ref<ActionJoin> join = actions[i];
		if (join.is_valid()) {
			join_inputs.insert(join->get_step_uuid(), HashSet<StringName>());
		}
	}
	if (!join_inputs.is_empty()) {
		for (int i = 0; i < actions.size(); i++) {
			Ref<SequenceAction> action = actions[i];
			if (action.is_null()) {
				continue;
			}
			Ref<ActionBranchCondition> branch = action;
			PackedStringArray destinations = action->get_next_steps();
			if (branch.is_valid()) {
				destinations = branch->get_true_steps();
				destinations.append_array(branch->get_false_steps());
			}
			for (const String &destination : destinations) {
				HashSet<StringName> *inputs = join_inputs.getptr(destination);
				if (inputs) {
					inputs->insert(action->get_step_uuid());
				}
			}
		}
	}
	playing = true;
	set_process(true);

	dispatch_depth++;
	emit_signal(SNAME("sequence_started"), current_sequence->get_sequence_id());

	StringName entry = current_sequence->get_entry_step_uuid();
	if (entry == StringName() && current_sequence->is_using_first_as_entry() && current_sequence->get_steps().size() > 0) {
		Ref<SequenceAction> first = current_sequence->get_steps()[0];
		if (first.is_valid()) {
			entry = first->get_step_uuid();
		}
	}

	if (entry != StringName()) {
		_start_step(entry);
	}
	_end_dispatch();
}

void SequenceDirector::_end_dispatch() {
	// A synchronous action can release another branch before scheduling its own successors.
	dispatch_depth--;
	if (dispatch_depth == 0 && playing && active_steps.is_empty() && waiting_joins.is_empty()) {
		playing = false;
		set_process(false);
		emit_signal(SNAME("sequence_completed"), current_sequence->get_sequence_id());
	}
}

void SequenceDirector::stop() {
	playing = false;
	active_steps.clear();
	waiting_joins.clear();
	set_process(false);
}

void SequenceDirector::_start_step(const StringName &p_step_uuid, const StringName &p_predecessor) {
	if (!playing || current_sequence.is_null() || p_step_uuid == StringName()) {
		return;
	}
	Ref<SequenceAction> action = current_sequence->find_step(p_step_uuid);
	if (action.is_null()) {
		return;
	}

	HashSet<StringName> *inputs = join_inputs.getptr(p_step_uuid);
	if (inputs && p_predecessor != StringName()) {
		if (!inputs->erase(p_predecessor)) {
			return;
		}
		if (!inputs->is_empty()) {
			waiting_joins.insert(p_step_uuid);
			_send_trace(p_step_uuid, "awaiting");
			return;
		}
		waiting_joins.erase(p_step_uuid);
	}

	emit_signal(SNAME("step_started"), current_sequence->get_sequence_id(), p_step_uuid);
	_send_trace(p_step_uuid, "active");

	action->execute(this, WorldState::get_singleton(), EventBus::get_singleton());

	Ref<ActionWaitEvent> wait_ev = action;
	Ref<ActionBlendCamera> blend_cam = action;

	double wait_duration = action->get_duration_sec();
	if (blend_cam.is_valid() && wait_duration <= 0.0) {
		wait_duration = blend_cam->get_blend_duration();
	}

	if (wait_ev.is_valid() || wait_duration > 0.0) {
		ActiveStep active;
		active.action = action;
		active.remaining_time = wait_duration;
		if (wait_ev.is_valid()) {
			active.waiting_for_event = true;
			active.expected_event_id = wait_ev->get_expected_event_id();
			active.remaining_time = wait_ev->get_timeout_sec();
			active.has_timeout = active.remaining_time > 0.0;
		}
		PackedStringArray immediate;
		if (!action->get_wait_for_completion()) {
			// Ordinary successors can start immediately. Join tokens represent actual completion.
			for (const String &target : action->resolve_next_steps(WorldState::get_singleton())) {
				if (join_inputs.has(target)) {
					active.deferred_joins.push_back(target);
				} else {
					immediate.push_back(target);
				}
			}
		}
		active_steps.push_back(active);
		if (active.waiting_for_event || action->get_wait_for_completion()) {
			_send_trace(p_step_uuid, "awaiting");
		}
		for (const String &target : immediate) {
			_start_step(target, p_step_uuid);
		}
		return;
	}

	completed_steps.insert(p_step_uuid);
	emit_signal(SNAME("step_completed"), current_sequence->get_sequence_id(), p_step_uuid);
	_send_trace(p_step_uuid, "completed");
	for (const String &target : action->resolve_next_steps(WorldState::get_singleton())) {
		_start_step(target, p_step_uuid);
	}
}

void SequenceDirector::_complete_active_step(const ActiveStep &p_step, const StringName &p_event_id, const Dictionary &p_payload) {
	if (!playing) {
		return;
	}
	Ref<SequenceAction> action = p_step.action;
	PackedStringArray next = p_step.deferred_joins;

	StringName uuid = action->get_step_uuid();
	completed_steps.insert(uuid);
	emit_signal(SNAME("step_completed"), current_sequence->get_sequence_id(), uuid);
	_send_trace(uuid, "completed");

	if (action->get_wait_for_completion()) {
		next = action->resolve_next_steps(WorldState::get_singleton(), p_event_id, p_payload);
	}
	for (const String &target : next) {
		_start_step(target, uuid);
	}
}

void SequenceDirector::step(double p_delta) {
	if (!playing || active_steps.is_empty()) {
		return;
	}

	LocalVector<ActiveStep> due;
	for (int i = (int)active_steps.size() - 1; i >= 0; i--) {
		ActiveStep &s = active_steps[i];
		if (s.waiting_for_event && !s.has_timeout) {
			continue;
		}
		s.remaining_time -= p_delta;
		if (s.remaining_time <= 0.0) {
			due.push_back(s);
			active_steps.remove_at(i);
		}
	}
	dispatch_depth++;
	for (const ActiveStep &s : due) {
		_complete_active_step(s);
	}
	_end_dispatch();
}

void SequenceDirector::notify_event(const StringName &p_event_id, const Dictionary &p_payload) {
	if (!playing || active_steps.is_empty()) {
		return;
	}

	// Detach all matching waits before a successor can emit another event into this director.
	LocalVector<ActiveStep> due;
	for (int i = (int)active_steps.size() - 1; i >= 0; i--) {
		if (active_steps[i].waiting_for_event && active_steps[i].expected_event_id == p_event_id) {
			due.push_back(active_steps[i]);
			active_steps.remove_at(i);
		}
	}
	dispatch_depth++;
	for (const ActiveStep &s : due) {
		_complete_active_step(s, p_event_id, p_payload);
	}
	_end_dispatch();
}

PackedStringArray SequenceDirector::get_active_step_uuids() const {
	PackedStringArray uuids;
	for (uint32_t i = 0; i < active_steps.size(); i++) {
		if (active_steps[i].action.is_valid()) {
			uuids.push_back(String(active_steps[i].action->get_step_uuid()));
		}
	}
	for (const StringName &join : waiting_joins) {
		uuids.push_back(join);
	}
	return uuids;
}

void SequenceDirector::_bind_methods() {
	ClassDB::bind_method(D_METHOD("play_sequence", "sequence"), &SequenceDirector::play_sequence);
	ClassDB::bind_method(D_METHOD("stop"), &SequenceDirector::stop);
	ClassDB::bind_method(D_METHOD("step", "delta"), &SequenceDirector::step);
	ClassDB::bind_method(D_METHOD("notify_event", "event_id", "payload"), &SequenceDirector::notify_event, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("is_playing"), &SequenceDirector::is_playing);
	ClassDB::bind_method(D_METHOD("get_current_sequence"), &SequenceDirector::get_current_sequence);
	ClassDB::bind_method(D_METHOD("get_active_step_uuids"), &SequenceDirector::get_active_step_uuids);
	ClassDB::bind_method(D_METHOD("is_step_completed", "step_uuid"), &SequenceDirector::is_step_completed);

	ADD_SIGNAL(MethodInfo("sequence_started", PropertyInfo(Variant::STRING_NAME, "sequence_id")));
	ADD_SIGNAL(MethodInfo("step_started", PropertyInfo(Variant::STRING_NAME, "sequence_id"), PropertyInfo(Variant::STRING_NAME, "step_uuid")));
	ADD_SIGNAL(MethodInfo("step_completed", PropertyInfo(Variant::STRING_NAME, "sequence_id"), PropertyInfo(Variant::STRING_NAME, "step_uuid")));
	ADD_SIGNAL(MethodInfo("sequence_completed", PropertyInfo(Variant::STRING_NAME, "sequence_id")));
}
