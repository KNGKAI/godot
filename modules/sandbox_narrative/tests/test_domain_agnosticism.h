#pragma once

#include "core/object/message_queue.h"
#include "modules/sandbox_core/event_bus.h"
#include "modules/sandbox_core/world_state.h"
#include "modules/sandbox_narrative/actor_registry.h"
#include "modules/sandbox_narrative/mission_data.h"
#include "modules/sandbox_narrative/mission_runner.h"
#include "modules/sandbox_narrative/sandbox_actor.h"
#include "modules/sandbox_narrative/sandbox_condition.h"
#include "modules/sandbox_narrative/sequence_action.h"
#include "modules/sandbox_narrative/sequence_data.h"
#include "modules/sandbox_narrative/sequence_director.h"
#include "scene/3d/node_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/resources/animation.h"
#include "tests/test_macros.h"

namespace TestSandboxDomainAgnosticism {

struct ArchetypeRig {
	Node3D *rig_root = nullptr;
	SandboxActor *actor = nullptr;

	void setup(const String &p_name, const StringName &p_actor_id, const StringName &p_archetype_tag) {
		rig_root = memnew(Node3D);
		rig_root->set_name(p_name);
		actor = memnew(SandboxActor);
		actor->set_actor_id(p_actor_id);
		TypedArray<StringName> tags;
		tags.push_back(p_archetype_tag);
		actor->set_tags(tags);
		rig_root->add_child(actor);
		if (ActorRegistry::get_singleton()) {
			ActorRegistry::get_singleton()->register_actor(actor);
		}
	}

	void teardown() {
		if (ActorRegistry::get_singleton() && actor) {
			ActorRegistry::get_singleton()->unregister_actor(actor);
		}
		if (rig_root) {
			memdelete(rig_root);
			rig_root = nullptr;
			actor = nullptr;
		}
	}
};

static Ref<Animation> _build_shared_cutscene_animation() {
	Ref<Animation> anim;
	anim.instantiate();
	anim->set_length(2.0);

	// 1. TYPE_EVENT_BUS track
	int eb_track = anim->add_track(Animation::TYPE_EVENT_BUS);
	anim->track_set_path(eb_track, NodePath("EventBus:cutscene.cue"));
	Dictionary payload;
	payload["intensity"] = 100;
	anim->event_bus_track_insert_key(eb_track, 0.5, SNAME("cutscene.cue"), payload);

	// 2. TYPE_WORLD_STATE track
	int ws_track = anim->add_track(Animation::TYPE_WORLD_STATE);
	anim->track_set_path(ws_track, NodePath("WorldState:env.fog_density"));
	anim->world_state_track_insert_key(ws_track, 0.0, 0.0);
	anim->world_state_track_insert_key(ws_track, 1.0, 1.0);

	// 3. TYPE_ACTOR_COMMAND track
	int ac_track = anim->add_track(Animation::TYPE_ACTOR_COMMAND);
	anim->track_set_path(ac_track, NodePath("ActorRegistry:primary_rig"));
	Dictionary action_payload;
	action_payload["pose"] = "alert";
	anim->actor_command_track_insert_key(ac_track, 0.25, SNAME("primary_rig"), SNAME("focus_target"), action_payload, true, true);

	return anim;
}

static Ref<SequenceData> _build_shared_sequence() {
	Ref<SequenceData> seq;
	seq.instantiate();
	seq->set_sequence_id(SNAME("seq_intro"));
	seq->set_entry_step_uuid(SNAME("step_lock"));

	Ref<ActionLockActor> step_lock;
	step_lock.instantiate();
	step_lock->set_step_uuid(SNAME("step_lock"));
	step_lock->set_actor_id(SNAME("primary_rig"));
	step_lock->set_locked(true);
	step_lock->set_reason("cutscene");
	PackedStringArray next_after_lock;
	next_after_lock.push_back("step_set_state");
	step_lock->set_next_steps(next_after_lock);

	Ref<ActionSetState> step_state;
	step_state.instantiate();
	step_state->set_step_uuid(SNAME("step_set_state"));
	step_state->set_key(SNAME("mission.stage"));
	step_state->set_value(1);
	PackedStringArray next_after_state;
	next_after_state.push_back("step_branch");
	step_state->set_next_steps(next_after_state);

	Ref<CondStateEquals> branch_cond;
	branch_cond.instantiate();
	branch_cond->set_key(SNAME("mission.stage"));
	branch_cond->set_expected(1);

	Ref<ActionBranchCondition> step_branch;
	step_branch.instantiate();
	step_branch->set_step_uuid(SNAME("step_branch"));
	step_branch->set_condition(branch_cond);
	PackedStringArray true_steps;
	true_steps.push_back("step_emit");
	step_branch->set_true_steps(true_steps);

	Ref<ActionEmitEvent> step_emit;
	step_emit.instantiate();
	step_emit->set_step_uuid(SNAME("step_emit"));
	step_emit->set_event_id(SNAME("objective.reached"));
	PackedStringArray next_after_emit;
	next_after_emit.push_back("step_unlock");
	step_emit->set_next_steps(next_after_emit);

	Ref<ActionLockActor> step_unlock;
	step_unlock.instantiate();
	step_unlock->set_step_uuid(SNAME("step_unlock"));
	step_unlock->set_actor_id(SNAME("primary_rig"));
	step_unlock->set_locked(false);

	TypedArray<SequenceAction> steps;
	steps.push_back(step_lock);
	steps.push_back(step_state);
	steps.push_back(step_branch);
	steps.push_back(step_emit);
	steps.push_back(step_unlock);
	seq->set_steps(steps);

	return seq;
}

static Ref<MissionData> _build_shared_mission() {
	Ref<MissionData> mission;
	mission.instantiate();
	mission->set_mission_id(SNAME("mission_recon"));
	mission->set_entry_objective_id(SNAME("obj_init"));

	Ref<CondStateEquals> cond_stage_1;
	cond_stage_1.instantiate();
	cond_stage_1->set_key(SNAME("mission.stage"));
	cond_stage_1->set_expected(1);

	Ref<ObjectiveData> obj_init;
	obj_init.instantiate();
	obj_init->set_objective_id(SNAME("obj_init"));
	obj_init->set_title("Initialize Recon");
	obj_init->set_complete_condition(cond_stage_1);

	Ref<CondEventFired> cond_event;
	cond_event.instantiate();
	cond_event->set_expected_event_id(SNAME("objective.reached"));

	Ref<ObjectiveData> obj_finish;
	obj_finish.instantiate();
	obj_finish->set_objective_id(SNAME("obj_finish"));
	obj_finish->set_title("Complete Recon");
	obj_finish->set_complete_condition(cond_event);

	Ref<MissionTransition> trans;
	trans.instantiate();
	trans->set_from_objective_id(SNAME("obj_init"));
	trans->set_to_objective_id(SNAME("obj_finish"));

	TypedArray<ObjectiveData> objectives;
	objectives.push_back(obj_init);
	objectives.push_back(obj_finish);
	mission->set_objectives(objectives);

	TypedArray<MissionTransition> transitions;
	transitions.push_back(trans);
	mission->set_transitions(transitions);

	return mission;
}

TEST_CASE("[Sandbox][Gate3] Domain Agnosticism across 1st-Person and RTS Archetypes") {
	WorldState *ws = WorldState::get_singleton();
	EventBus *eb = EventBus::get_singleton();
	ActorRegistry *ar = ActorRegistry::get_singleton();
	MissionRunner *mr = MissionRunner::get_singleton();

	REQUIRE(ws != nullptr);
	REQUIRE(eb != nullptr);
	REQUIRE(ar != nullptr);
	REQUIRE(mr != nullptr);

	Ref<Animation> shared_anim = _build_shared_cutscene_animation();
	Ref<SequenceData> shared_seq = _build_shared_sequence();
	Ref<MissionData> shared_mission = _build_shared_mission();

	// Verify custom tracks on Animation resource directly.
	CHECK(shared_anim->get_track_count() == 3);
	CHECK(shared_anim->track_get_type(0) == Animation::TYPE_EVENT_BUS);
	CHECK(shared_anim->track_get_type(1) == Animation::TYPE_WORLD_STATE);
	CHECK(shared_anim->track_get_type(2) == Animation::TYPE_ACTOR_COMMAND);
	CHECK(shared_anim->event_bus_track_get_key_event_id(0, 0) == SNAME("cutscene.cue"));
	CHECK(Math::is_equal_approx((double)shared_anim->world_state_track_interpolate(1, 0.5), 0.5));

	const char *archetypes[2] = { "FirstPersonCharacterRig", "RTSTopDownCameraRig" };
	const char *tags[2] = { "first_person", "rts_camera" };

	for (int i = 0; i < 2; i++) {
		ws->clear();
		eb->clear_all();
		ar->clear();
		mr->clear();

		ArchetypeRig rig;
		rig.setup(archetypes[i], SNAME("primary_rig"), StringName(tags[i]));
		CHECK(ar->get_actor(SNAME("primary_rig")) == rig.actor);
		CHECK(ar->get_actors_by_tag(StringName(tags[i])).size() == 1);

		// Dispatch custom track actor command via ActorRegistry
		Dictionary cmd = shared_anim->track_get_key_value(2, 0);
		ar->send_control_lock(cmd["actor_id"], cmd["locked"], "anim_track");
		ar->send_action(cmd["actor_id"], cmd["action_id"], cmd["payload"]);
		CHECK(rig.actor->is_control_locked());
		CHECK(rig.actor->get_last_action_id() == SNAME("focus_target"));

		// Run identical MissionData & SequenceData on this archetype
		mr->register_mission(shared_mission);
		CHECK(mr->start_mission(SNAME("mission_recon")));
		CHECK(mr->get_mission_status(SNAME("mission_recon")) == MissionRunner::STATUS_ACTIVE);
		CHECK(mr->is_objective_active(SNAME("mission_recon"), SNAME("obj_init")));

		SequenceDirector *director = memnew(SequenceDirector);
		director->play_sequence(shared_seq);

		// Sequence should have executed step_lock -> step_set_state -> step_branch -> step_emit -> step_unlock
		CHECK(!director->is_playing());
		CHECK(director->is_step_completed(SNAME("step_lock")));
		CHECK(director->is_step_completed(SNAME("step_set_state")));
		CHECK(director->is_step_completed(SNAME("step_branch")));
		CHECK(director->is_step_completed(SNAME("step_emit")));
		CHECK(director->is_step_completed(SNAME("step_unlock")));
		CHECK(!rig.actor->is_control_locked());
		CHECK((int)ws->get_value(SNAME("mission.stage")) == 1);

		// MissionRunner should have transitioned obj_init -> obj_finish and completed mission_recon
		CHECK(mr->is_objective_completed(SNAME("mission_recon"), SNAME("obj_init")));
		CHECK(mr->is_objective_completed(SNAME("mission_recon"), SNAME("obj_finish")));
		CHECK(mr->get_mission_status(SNAME("mission_recon")) == MissionRunner::STATUS_COMPLETED);

		memdelete(director);
		rig.teardown();
	}
}

TEST_CASE("[SceneTree][Sandbox][Gate3] Mission lifecycle sequences play and release a wait from the event bus") {
	MissionRunner *runner = MissionRunner::get_singleton();
	WorldState *state = WorldState::get_singleton();
	runner->clear();
	state->clear();

	Ref<ActionSetState> start;
	start.instantiate();
	start->set_step_uuid("start");
	start->set_key("gate3.started");
	start->set_value(true);
	Ref<SequenceData> intro;
	intro.instantiate();
	TypedArray<SequenceAction> start_steps;
	start_steps.push_back(start);
	intro->set_steps(start_steps);

	Ref<ActionWaitEvent> wait;
	wait.instantiate();
	wait->set_step_uuid("wait");
	wait->set_expected_event_id("gate3.release");
	wait->set_next_steps({ "finish" });
	Ref<ActionSetState> finish;
	finish.instantiate();
	finish->set_step_uuid("finish");
	finish->set_key("gate3.finished");
	finish->set_value(true);
	Ref<SequenceData> outro;
	outro.instantiate();
	TypedArray<SequenceAction> finish_steps;
	finish_steps.push_back(wait);
	finish_steps.push_back(finish);
	outro->set_steps(finish_steps);

	Ref<MissionData> mission;
	mission.instantiate();
	mission->set_mission_id("gate3.lifecycle");
	mission->set_on_start_sequence(intro);
	mission->set_on_complete_sequence(outro);
	runner->register_mission(mission);
	CHECK(runner->start_mission("gate3.lifecycle"));
	MessageQueue::get_singleton()->flush();
	CHECK(state->get_bool("gate3.started"));
	CHECK(runner->get_mission_status("gate3.lifecycle") == MissionRunner::STATUS_COMPLETED);
	CHECK_FALSE(state->get_bool("gate3.finished"));
	EventBus::get_singleton()->emit("gate3.release");
	CHECK(state->get_bool("gate3.finished"));

	state->clear();
	CHECK(runner->start_mission("gate3.lifecycle"));
	runner->clear();
	MessageQueue::get_singleton()->flush();
	EventBus::get_singleton()->emit("gate3.release");
	CHECK_FALSE(state->get_bool("gate3.started"));
	CHECK_FALSE(state->get_bool("gate3.finished"));
}

TEST_CASE("[SceneTree][Sandbox][Gate3] An event released during a fork keeps the emitting branch alive") {
	Ref<SequenceAction> fork;
	fork.instantiate();
	fork->set_step_uuid("fork");
	fork->set_next_steps({ "wait", "emit" });
	Ref<ActionWaitEvent> wait;
	wait.instantiate();
	wait->set_step_uuid("wait");
	wait->set_expected_event_id("gate3.release");
	Ref<ActionEmitEvent> emit;
	emit.instantiate();
	emit->set_step_uuid("emit");
	emit->set_event_id("gate3.release");
	emit->set_next_steps({ "hold" });
	Ref<SequenceAction> hold;
	hold.instantiate();
	hold->set_step_uuid("hold");
	hold->set_duration_sec(1);

	TypedArray<SequenceAction> steps;
	steps.push_back(fork);
	steps.push_back(wait);
	steps.push_back(emit);
	steps.push_back(hold);
	Ref<SequenceData> sequence;
	sequence.instantiate();
	sequence->set_steps(steps);
	SequenceDirector *director = memnew(SequenceDirector);
	SceneTree::get_singleton()->get_root()->add_child(director);
	director->play_sequence(sequence);
	CHECK(director->is_step_completed("wait"));
	CHECK(director->is_playing());
	director->step(1);
	CHECK(director->is_step_completed("hold"));
	CHECK_FALSE(director->is_playing());
	memdelete(director);
}

} // namespace TestSandboxDomainAgnosticism
