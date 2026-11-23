#include "mission_runner.h"

#include "core/debugger/engine_debugger.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"

#include "modules/sandbox_core/command_bus.h"
#include "modules/sandbox_core/event_bus.h"
#include "modules/sandbox_core/sandbox_clock.h"
#include "modules/sandbox_core/sandbox_serializer.h"
#include "modules/sandbox_core/world_state.h"
#include "modules/sandbox_narrative/sequence_director.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

MissionRunner *MissionRunner::singleton = nullptr;

void MissionRunner::_on_narrative_clock_tick(double p_delta) {
	if (singleton) {
		singleton->evaluate_all();
	}
}

void MissionRunner::_on_event_bus_emitted(const StringName &p_event_id, const Dictionary &p_payload) {
	if (singleton) {
		singleton->evaluate_all(p_event_id, p_payload);
	}
}

Dictionary MissionRunner::_export_missions_callback() {
	if (singleton) {
		return singleton->export_state();
	}
	return Dictionary();
}

Error MissionRunner::_import_missions_callback(const Dictionary &p_state) {
	if (singleton) {
		return singleton->import_state(p_state) ? OK : ERR_INVALID_DATA;
	}
	return OK;
}

void MissionRunner::_send_trace(const StringName &p_mission_id, const StringName &p_objective_id, const String &p_state) const {
	if (EngineDebugger::is_active()) {
		Array data;
		data.push_back("mission");
		data.push_back(String(p_mission_id));
		data.push_back(String(p_objective_id));
		data.push_back(p_state);
		EngineDebugger::get_singleton()->send_message("sandbox:narrative_trace", data);
	}
}

void MissionRunner::register_mission(const Ref<MissionData> &p_mission) {
	ERR_FAIL_COND(p_mission.is_null());
	StringName id = p_mission->get_mission_id();
	ERR_FAIL_COND(id == StringName());

	MissionInstance *previous = missions.getptr(id);
	if (previous) {
		_cancel_sequences(*previous);
	}
	MissionInstance inst;
	inst.data = p_mission;
	inst.status = STATUS_INACTIVE;
	missions[id] = inst;
}

void MissionRunner::clear() {
	for (KeyValue<StringName, MissionInstance> &entry : missions) {
		_cancel_sequences(entry.value);
	}
	missions.clear();
}

void MissionRunner::_cancel_sequences(MissionInstance &p_instance) {
	p_instance.playback_generation = ++next_playback_generation;
	for (ObjectID id : { p_instance.start_director, p_instance.completion_director }) {
		SequenceDirector *director = ObjectDB::get_instance<SequenceDirector>(id);
		if (director) {
			director->stop();
			director->queue_free();
		}
	}
	p_instance.start_director = ObjectID();
	p_instance.completion_director = ObjectID();
}

void MissionRunner::_queue_sequence(const MissionInstance &p_instance, bool p_completion) {
	if (p_instance.data.is_null() || (p_completion ? p_instance.data->get_on_complete_sequence() : p_instance.data->get_on_start_sequence()).is_null()) {
		return;
	}
	SceneTree *tree = SceneTree::get_singleton();
	ERR_FAIL_NULL(tree);
	Node *parent = tree->get_current_scene();
	if (!parent) {
		parent = tree->get_root();
	}
	// A mission may start in a child's _ready() while its scene is still adding children.
	callable_mp(this, &MissionRunner::_play_sequence).call_deferred(p_instance.data->get_mission_id(), p_instance.playback_generation, parent->get_instance_id(), p_completion);
}

void MissionRunner::_play_sequence(const StringName &p_mission_id, uint64_t p_generation, ObjectID p_parent_id, bool p_completion) {
	MissionInstance *instance = missions.getptr(p_mission_id);
	Node *parent = ObjectDB::get_instance<Node>(p_parent_id);
	if (!instance || instance->playback_generation != p_generation || !parent || !parent->is_inside_tree() || parent->is_queued_for_deletion()) {
		return;
	}
	Ref<SequenceData> sequence = p_completion ? instance->data->get_on_complete_sequence() : instance->data->get_on_start_sequence();
	if (sequence.is_null()) {
		return;
	}
	SequenceDirector *director = memnew(SequenceDirector);
	(p_completion ? instance->completion_director : instance->start_director) = director->get_instance_id();
	director->connect(SNAME("sequence_completed"), callable_mp(static_cast<Node *>(director), &Node::queue_free).unbind(1), Object::CONNECT_ONE_SHOT);
	parent->add_child(director, false, Node::INTERNAL_MODE_BACK);
	director->play_sequence(sequence);
}

bool MissionRunner::start_mission(const StringName &p_mission_id) {
	HashMap<StringName, MissionInstance>::Iterator it = missions.find(p_mission_id);
	if (it == missions.end() || it->value.data.is_null()) {
		return false;
	}

	MissionInstance &inst = it->value;
	if (inst.data->get_prerequisite().is_valid() && !inst.data->get_prerequisite()->evaluate(WorldState::get_singleton())) {
		return false;
	}
	_cancel_sequences(inst);
	inst.status = STATUS_ACTIVE;
	inst.active_objectives.clear();
	inst.completed_objectives.clear();
	inst.failed_objectives.clear();

	StringName entry = inst.data->get_entry_objective_id();
	if (entry == StringName() && inst.data->is_using_first_as_entry() && inst.data->get_objectives().size() > 0) {
		Ref<ObjectiveData> first = inst.data->get_objectives()[0];
		if (first.is_valid()) {
			entry = first->get_objective_id();
		}
	}

	if (entry != StringName()) {
		inst.active_objectives.insert(entry);
		_send_trace(p_mission_id, entry, "active");
	}

	_queue_sequence(inst, false);
	emit_signal(SNAME("mission_started"), p_mission_id);
	if (EventBus::get_singleton()) {
		Dictionary payload;
		payload["mission_id"] = p_mission_id;
		EventBus::get_singleton()->emit(SNAME("mission.started"), payload);
	}

	MissionInstance *current = missions.getptr(p_mission_id);
	if (current) {
		_evaluate_mission(*current);
	}
	return true;
}

bool MissionRunner::complete_mission(const StringName &p_mission_id) {
	HashMap<StringName, MissionInstance>::Iterator it = missions.find(p_mission_id);
	if (it == missions.end()) {
		return false;
	}
	if (it->value.status == STATUS_COMPLETED) {
		return true;
	}
	it->value.status = STATUS_COMPLETED;
	it->value.active_objectives.clear();
	_queue_sequence(it->value, true);
	emit_signal(SNAME("mission_completed"), p_mission_id);
	if (EventBus::get_singleton()) {
		Dictionary payload;
		payload["mission_id"] = p_mission_id;
		EventBus::get_singleton()->emit(SNAME("mission.completed"), payload);
	}
	return true;
}

bool MissionRunner::fail_mission(const StringName &p_mission_id) {
	HashMap<StringName, MissionInstance>::Iterator it = missions.find(p_mission_id);
	if (it == missions.end()) {
		return false;
	}
	_cancel_sequences(it->value);
	it->value.status = STATUS_FAILED;
	it->value.active_objectives.clear();
	emit_signal(SNAME("mission_failed"), p_mission_id);
	return true;
}

MissionRunner::MissionStatus MissionRunner::get_mission_status(const StringName &p_mission_id) const {
	HashMap<StringName, MissionInstance>::ConstIterator it = missions.find(p_mission_id);
	if (it == missions.end()) {
		return STATUS_INACTIVE;
	}
	return it->value.status;
}

PackedStringArray MissionRunner::get_active_objectives(const StringName &p_mission_id) const {
	PackedStringArray res;
	HashMap<StringName, MissionInstance>::ConstIterator it = missions.find(p_mission_id);
	if (it == missions.end()) {
		return res;
	}
	for (const StringName &obj_id : it->value.active_objectives) {
		res.push_back(String(obj_id));
	}
	return res;
}

PackedStringArray MissionRunner::get_completed_objectives(const StringName &p_mission_id) const {
	PackedStringArray res;
	HashMap<StringName, MissionInstance>::ConstIterator it = missions.find(p_mission_id);
	if (it == missions.end()) {
		return res;
	}
	for (const StringName &obj_id : it->value.completed_objectives) {
		res.push_back(String(obj_id));
	}
	return res;
}

bool MissionRunner::is_objective_active(const StringName &p_mission_id, const StringName &p_objective_id) const {
	HashMap<StringName, MissionInstance>::ConstIterator it = missions.find(p_mission_id);
	return it != missions.end() && it->value.active_objectives.has(p_objective_id);
}

bool MissionRunner::is_objective_completed(const StringName &p_mission_id, const StringName &p_objective_id) const {
	HashMap<StringName, MissionInstance>::ConstIterator it = missions.find(p_mission_id);
	return it != missions.end() && it->value.completed_objectives.has(p_objective_id);
}

void MissionRunner::_evaluate_mission(MissionInstance &p_inst, const StringName &p_event_id, const Dictionary &p_payload) {
	if (p_inst.status != STATUS_ACTIVE || p_inst.data.is_null()) {
		return;
	}

	WorldState *state = WorldState::get_singleton();
	bool changed = true;
	int safety_steps = 0;

	while (changed && safety_steps < 32) {
		changed = false;
		safety_steps++;

		LocalVector<StringName> current_active;
		for (const StringName &obj_id : p_inst.active_objectives) {
			current_active.push_back(obj_id);
		}

		for (const StringName &obj_id : current_active) {
			Ref<ObjectiveData> obj = p_inst.data->find_objective(obj_id);
			if (obj.is_null()) {
				continue;
			}

			bool failed = obj->get_fail_condition().is_valid() && obj->get_fail_condition()->evaluate(state, p_event_id, p_payload);
			if (failed) {
				p_inst.active_objectives.erase(obj_id);
				p_inst.failed_objectives.insert(obj_id);
				_send_trace(p_inst.data->get_mission_id(), obj_id, "failed");
				emit_signal(SNAME("objective_failed"), p_inst.data->get_mission_id(), obj_id);
				changed = true;

				bool transitioned = false;
				TypedArray<MissionTransition> transitions = p_inst.data->get_transitions();
				for (int i = 0; i < transitions.size(); i++) {
					Ref<MissionTransition> tr = transitions[i];
					if (tr.is_valid() && tr->get_from_objective_id() == obj_id && tr->is_on_fail()) {
						if (tr->get_condition().is_null() || tr->get_condition()->evaluate(state, p_event_id, p_payload)) {
							if (tr->get_to_objective_id() != StringName()) {
								p_inst.active_objectives.insert(tr->get_to_objective_id());
								_send_trace(p_inst.data->get_mission_id(), tr->get_to_objective_id(), "active");
								transitioned = true;
							}
						}
					}
				}

				if (!transitioned && obj->is_required()) {
					fail_mission(p_inst.data->get_mission_id());
					return;
				}
				continue;
			}

			bool completed = obj->get_complete_condition().is_valid() && obj->get_complete_condition()->evaluate(state, p_event_id, p_payload);
			if (completed) {
				p_inst.active_objectives.erase(obj_id);
				p_inst.completed_objectives.insert(obj_id);
				_send_trace(p_inst.data->get_mission_id(), obj_id, "completed");
				emit_signal(SNAME("objective_completed"), p_inst.data->get_mission_id(), obj_id);
				changed = true;

				TypedArray<MissionTransition> transitions = p_inst.data->get_transitions();
				for (int i = 0; i < transitions.size(); i++) {
					Ref<MissionTransition> tr = transitions[i];
					if (tr.is_valid() && tr->get_from_objective_id() == obj_id && !tr->is_on_fail()) {
						if (tr->get_condition().is_null() || tr->get_condition()->evaluate(state, p_event_id, p_payload)) {
							if (tr->get_to_objective_id() != StringName()) {
								p_inst.active_objectives.insert(tr->get_to_objective_id());
								_send_trace(p_inst.data->get_mission_id(), tr->get_to_objective_id(), "active");
							}
						}
					}
				}
			}
		}

		if (p_inst.active_objectives.is_empty() && p_inst.status == STATUS_ACTIVE) {
			complete_mission(p_inst.data->get_mission_id());
			return;
		}
	}
}

void MissionRunner::evaluate_all(const StringName &p_event_id, const Dictionary &p_payload) {
	for (KeyValue<StringName, MissionInstance> &E : missions) {
		if (E.value.status == STATUS_ACTIVE) {
			_evaluate_mission(E.value, p_event_id, p_payload);
		}
	}
}

Dictionary MissionRunner::export_state() const {
	Dictionary out;
	for (const KeyValue<StringName, MissionInstance> &E : missions) {
		Dictionary m;
		m["status"] = (int)E.value.status;
		Array active;
		for (const StringName &id : E.value.active_objectives) {
			active.push_back(String(id));
		}
		Array completed;
		for (const StringName &id : E.value.completed_objectives) {
			completed.push_back(String(id));
		}
		Array failed;
		for (const StringName &id : E.value.failed_objectives) {
			failed.push_back(String(id));
		}
		m["active_objectives"] = active;
		m["completed_objectives"] = completed;
		m["failed_objectives"] = failed;
		out[String(E.key)] = m;
	}
	return out;
}

bool MissionRunner::import_state(const Dictionary &p_state) {
	LocalVector<Variant> keys = p_state.get_key_list();
	for (const Variant &k : keys) {
		StringName mission_id = StringName(String(k));
		Dictionary m = p_state[k];
		MissionInstance &inst = missions[mission_id];
		_cancel_sequences(inst);
		inst.status = (MissionStatus)(int)m.get("status", STATUS_INACTIVE);
		inst.active_objectives.clear();
		inst.completed_objectives.clear();
		inst.failed_objectives.clear();

		Array active = m.get("active_objectives", Array());
		for (int i = 0; i < active.size(); i++) {
			inst.active_objectives.insert(StringName(String(active[i])));
		}
		Array completed = m.get("completed_objectives", Array());
		for (int i = 0; i < completed.size(); i++) {
			inst.completed_objectives.insert(StringName(String(completed[i])));
		}
		Array failed = m.get("failed_objectives", Array());
		for (int i = 0; i < failed.size(); i++) {
			inst.failed_objectives.insert(StringName(String(failed[i])));
		}
	}
	return true;
}

Variant MissionRunner::_cmd_mission_start(const String &p_id) {
	if (!missions.has(p_id)) {
		return Dictionary{ { "ok", false }, { "output", "Mission not found: " + p_id }, { "error_code", ERR_DOES_NOT_EXIST } };
	}
	if (!start_mission(p_id)) {
		return Dictionary{ { "ok", false }, { "output", "Mission cannot start: " + p_id }, { "error_code", ERR_UNCONFIGURED } };
	}
	return "Started mission: " + p_id;
}

Variant MissionRunner::_cmd_mission_complete(const String &p_id) {
	if (!complete_mission(p_id)) {
		return Dictionary{ { "ok", false }, { "output", "Mission not found: " + p_id }, { "error_code", ERR_DOES_NOT_EXIST } };
	}
	return "Completed mission: " + p_id;
}

void MissionRunner::_bind_methods() {
	ClassDB::bind_method(D_METHOD("register_mission", "mission"), &MissionRunner::register_mission);
	ClassDB::bind_method(D_METHOD("clear"), &MissionRunner::clear);
	ClassDB::bind_method(D_METHOD("start_mission", "mission_id"), &MissionRunner::start_mission);
	ClassDB::bind_method(D_METHOD("complete_mission", "mission_id"), &MissionRunner::complete_mission);
	ClassDB::bind_method(D_METHOD("fail_mission", "mission_id"), &MissionRunner::fail_mission);
	ClassDB::bind_method(D_METHOD("get_mission_status", "mission_id"), &MissionRunner::get_mission_status);
	ClassDB::bind_method(D_METHOD("get_active_objectives", "mission_id"), &MissionRunner::get_active_objectives);
	ClassDB::bind_method(D_METHOD("get_completed_objectives", "mission_id"), &MissionRunner::get_completed_objectives);
	ClassDB::bind_method(D_METHOD("is_objective_active", "mission_id", "objective_id"), &MissionRunner::is_objective_active);
	ClassDB::bind_method(D_METHOD("is_objective_completed", "mission_id", "objective_id"), &MissionRunner::is_objective_completed);
	ClassDB::bind_method(D_METHOD("evaluate_all", "event_id", "payload"), &MissionRunner::evaluate_all, DEFVAL(StringName()), DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("export_state"), &MissionRunner::export_state);
	ClassDB::bind_method(D_METHOD("import_state", "state"), &MissionRunner::import_state);

	ADD_SIGNAL(MethodInfo("mission_started", PropertyInfo(Variant::STRING_NAME, "mission_id")));
	ADD_SIGNAL(MethodInfo("mission_completed", PropertyInfo(Variant::STRING_NAME, "mission_id")));
	ADD_SIGNAL(MethodInfo("mission_failed", PropertyInfo(Variant::STRING_NAME, "mission_id")));
	ADD_SIGNAL(MethodInfo("objective_completed", PropertyInfo(Variant::STRING_NAME, "mission_id"), PropertyInfo(Variant::STRING_NAME, "objective_id")));
	ADD_SIGNAL(MethodInfo("objective_failed", PropertyInfo(Variant::STRING_NAME, "mission_id"), PropertyInfo(Variant::STRING_NAME, "objective_id")));

	BIND_ENUM_CONSTANT(STATUS_INACTIVE);
	BIND_ENUM_CONSTANT(STATUS_ACTIVE);
	BIND_ENUM_CONSTANT(STATUS_COMPLETED);
	BIND_ENUM_CONSTANT(STATUS_FAILED);
}

MissionRunner::MissionRunner() {
	singleton = this;

	if (SandboxClock::get_singleton()) {
		SandboxClock::get_singleton()->set_narrative_update_callback(&MissionRunner::_on_narrative_clock_tick);
	}
	if (EventBus::get_singleton()) {
		EventBus::get_singleton()->add_native_observer(&MissionRunner::_on_event_bus_emitted);
	}
	if (SandboxSerializer::get_singleton()) {
		SandboxSerializer::get_singleton()->set_missions_callbacks(&MissionRunner::_export_missions_callback, &MissionRunner::_import_missions_callback);
	}
	if (CommandBus::get_singleton()) {
		TypedArray<int> id_arg;
		id_arg.push_back((int)Variant::STRING);
		CommandBus::get_singleton()->register_command("mission.start", CommandBus::PERMISSION_ADMIN, id_arg, callable_mp(this, &MissionRunner::_cmd_mission_start), "Start a registered mission");
		CommandBus::get_singleton()->register_command("mission.complete", CommandBus::PERMISSION_ADMIN, id_arg, callable_mp(this, &MissionRunner::_cmd_mission_complete), "Force-complete a mission");
		CommandBus::get_singleton()->register_command("mission.status", CommandBus::PERMISSION_USER, TypedArray<int>(), callable_mp(this, &MissionRunner::export_state), "Inspect active mission status");
	}
}

MissionRunner::~MissionRunner() {
	if (SandboxClock::get_singleton()) {
		SandboxClock::get_singleton()->set_narrative_update_callback(nullptr);
	}
	if (EventBus::get_singleton()) {
		EventBus::get_singleton()->remove_native_observer(&MissionRunner::_on_event_bus_emitted);
	}
	if (SandboxSerializer::get_singleton()) {
		SandboxSerializer::get_singleton()->set_missions_callbacks(nullptr, nullptr);
	}
	if (singleton == this) {
		singleton = nullptr;
	}
}
