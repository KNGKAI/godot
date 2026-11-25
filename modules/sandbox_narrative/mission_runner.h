#pragma once

#include "core/object/object.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/variant/dictionary.h"
#include "modules/sandbox_narrative/mission_data.h"

class MissionRunner : public Object {
	GDCLASS(MissionRunner, Object);

public:
	enum MissionStatus {
		STATUS_INACTIVE = 0,
		STATUS_ACTIVE = 1,
		STATUS_COMPLETED = 2,
		STATUS_FAILED = 3,
	};

private:
	struct MissionInstance {
		Ref<MissionData> data;
		MissionStatus status = STATUS_INACTIVE;
		HashSet<StringName> active_objectives;
		HashSet<StringName> completed_objectives;
		HashSet<StringName> failed_objectives;
		uint64_t playback_generation = 0;
		ObjectID start_director;
		ObjectID completion_director;
	};

	static MissionRunner *singleton;
	HashMap<StringName, MissionInstance> missions;
	uint64_t next_playback_generation = 0;

	void _cancel_sequences(MissionInstance &p_instance);
	void _queue_sequence(const MissionInstance &p_instance, bool p_completion);
	void _play_sequence(const StringName &p_mission_id, uint64_t p_generation, ObjectID p_parent_id, bool p_completion);

	static void _on_narrative_clock_tick(double p_delta);
	static void _on_event_bus_emitted(const StringName &p_event_id, const Dictionary &p_payload);
	static Dictionary _export_missions_callback();
	static Error _import_missions_callback(const Dictionary &p_state);

	void _evaluate_mission(MissionInstance &p_inst, const StringName &p_event_id = StringName(), const Dictionary &p_payload = Dictionary());
	void _send_trace(const StringName &p_mission_id, const StringName &p_objective_id, const String &p_state) const;

	Variant _cmd_mission_start(const String &p_id);
	Variant _cmd_mission_complete(const String &p_id);

protected:
	static void _bind_methods();

public:
	static MissionRunner *get_singleton() { return singleton; }

	void register_mission(const Ref<MissionData> &p_mission);
	void clear();

	bool start_mission(const StringName &p_mission_id);
	bool complete_mission(const StringName &p_mission_id);
	bool fail_mission(const StringName &p_mission_id);

	MissionStatus get_mission_status(const StringName &p_mission_id) const;
	PackedStringArray get_active_objectives(const StringName &p_mission_id) const;
	PackedStringArray get_completed_objectives(const StringName &p_mission_id) const;
	bool is_objective_active(const StringName &p_mission_id, const StringName &p_objective_id) const;
	bool is_objective_completed(const StringName &p_mission_id, const StringName &p_objective_id) const;

	void evaluate_all(const StringName &p_event_id = StringName(), const Dictionary &p_payload = Dictionary());

	Dictionary export_state() const;
	bool import_state(const Dictionary &p_state);

	MissionRunner();
	~MissionRunner() override;
};

VARIANT_ENUM_CAST(MissionRunner::MissionStatus);
