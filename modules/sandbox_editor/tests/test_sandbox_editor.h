#pragma once

#include "core/object/message_queue.h"
#include "core/object/undo_redo.h"
#include "scene/gui/button.h"
#include "scene/gui/graph_edit.h"
#include "scene/gui/graph_frame.h"
#include "scene/gui/graph_node.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "tests/test_macros.h"

#include "modules/sandbox_editor/sandbox_narrative_graph_editor.h"
#include "modules/sandbox_narrative/mission_data.h"
#include "modules/sandbox_narrative/sequence_director.h"

namespace TestSandboxNarrativeGraph {

struct CanvasFixture {
	UndoRedo history;
	SandboxNarrativeGraphEditor *editor = memnew(SandboxNarrativeGraphEditor(&history));
	GraphEdit *canvas = editor->get_graph_edit();

	CanvasFixture() {
		SceneTree::get_singleton()->get_root()->add_child(editor);
	}
	~CanvasFixture() {
		memdelete(editor);
		MessageQueue::get_singleton()->flush();
	}
	StringName add(const StringName &p_class) {
		StringName key = editor->add_graph_node(p_class, Vector2(20, 30));
		MessageQueue::get_singleton()->flush();
		return key;
	}
	void link(const StringName &p_from, int p_output, const StringName &p_to, int p_input, bool p_connect = true) {
		canvas->emit_signal(p_connect ? SNAME("connection_request") : SNAME("disconnection_request"), p_from, p_output, p_to, p_input);
		MessageQueue::get_singleton()->flush();
	}
};

TEST_CASE("[SceneTree][SandboxEditor][Gate8] Mission canvas authors a branching fifteen-node mission, synchronized undo and live trace") {
	CanvasFixture graph;
	Ref<MissionData> mission;
	mission.instantiate();
	mission->set_mission_id("rescue");
	graph.editor->edit(mission);
	StringName first = graph.add("ObjectiveData");
	StringName success = graph.add("ObjectiveData");
	StringName failure = graph.add("ObjectiveData");
	StringName all = graph.add("CondAll");
	StringName any = graph.add("CondAny");
	StringName negate = graph.add("CondNot");
	StringName state = graph.add("CondStateEquals");
	StringName range = graph.add("CondStateRange");
	StringName event = graph.add("CondEventFired");
	StringName seq = graph.add("SequenceData");
	StringName frame = graph.add("GraphFrame");
	for (int i = 0; i < 4; i++) {
		CHECK(graph.add("ObjectiveData") != StringName());
	}
	REQUIRE(mission->get_objectives().size() == 7);
	Ref<ObjectiveData> initial = mission->get_objectives()[0];
	graph.link(first, 0, success, 0);
	graph.link(first, 1, failure, 0);
	REQUIRE(mission->get_transitions().size() == 2);
	CHECK(Ref<MissionTransition>(mission->get_transitions()[1])->is_on_fail());
	graph.link(state, 0, all, 0);
	graph.link(range, 0, any, 0);
	graph.link(event, 0, any, 0);
	graph.link(any, 0, all, 0);
	graph.link(all, 0, first, 1);
	graph.link(event, 0, negate, 0);
	graph.link(negate, 0, first, 2);
	graph.link("entry", 1, seq, 0);
	CHECK(mission->get_on_start_sequence().is_valid());
	Ref<CondAll> completion = initial->get_complete_condition();
	REQUIRE(completion.is_valid());
	if (completion.is_null()) {
		return;
	}
	REQUIRE(completion->get_children().size() == 2);
	Ref<CondAny> alternatives = completion->get_children()[1];
	REQUIRE(alternatives.is_valid());
	CHECK(alternatives->get_children().size() == 2);
	REQUIRE(initial->get_fail_condition().is_valid());
	graph.link(all, 0, any, 0); // No recursive condition cycles.
	CHECK(alternatives->get_children().size() == 2);
	graph.link(state, 0, success, 0); // No logic-to-execution connections.
	CHECK(mission->get_transitions().size() == 2);
	graph.link("entry", 0, first, 0, false);
	CHECK(mission->get_entry_objective_id() == StringName());
	CHECK_FALSE(graph.canvas->is_node_connected("entry", 0, first, 0));
	CHECK(graph.history.undo());
	MessageQueue::get_singleton()->flush();
	CHECK(graph.canvas->is_node_connected("entry", 0, first, 0));
	TypedArray<StringName> selection;
	selection.push_back(first);
	selection.push_back(frame);
	graph.canvas->emit_signal("delete_nodes_request", selection);
	MessageQueue::get_singleton()->flush();
	CHECK(mission->get_objectives().size() == 6);
	CHECK(mission->get_transitions().is_empty());
	REQUIRE(graph.history.undo());
	MessageQueue::get_singleton()->flush();
	CHECK(mission->get_objectives().size() == 7);
	CHECK(mission->get_transitions().size() == 2);
	CHECK(initial->get_complete_condition() == completion);
	REQUIRE(graph.history.redo());
	MessageQueue::get_singleton()->flush();
	CHECK(mission->get_objectives().size() == 6);
	REQUIRE(graph.history.undo());
	MessageQueue::get_singleton()->flush();

	initial->set_title("Inspector edit");
	MessageQueue::get_singleton()->flush();
	GraphNode *node = Object::cast_to<GraphNode>(graph.canvas->get_node(NodePath(first)));
	REQUIRE(node);
	CHECK(node->get_title() == "Inspector edit");
	Array trace;
	trace.push_back("mission");
	trace.push_back("another mission");
	trace.push_back(initial->get_objective_id());
	trace.push_back("active");
	graph.editor->apply_trace(trace);
	CHECK_FALSE(node->has_meta("trace_state"));
	trace[1] = "rescue";
	graph.editor->apply_trace(trace);
	CHECK(node->get_self_modulate().g > node->get_self_modulate().r);
	trace[3] = "failed";
	graph.editor->apply_trace(trace);
	CHECK(node->get_self_modulate().r > node->get_self_modulate().g);
	graph.editor->clear_trace();
	CHECK_FALSE(node->has_meta("trace_state"));
}

TEST_CASE("[SceneTree][SandboxEditor][Gate8] Sequence canvas authors a parallel fork, guarded alternatives and frame placement") {
	CanvasFixture graph;
	REQUIRE(graph.editor->get_empty_prompt() != nullptr);
	CHECK(graph.editor->get_empty_prompt()->is_visible());
	Ref<SequenceData> sequence;
	sequence.instantiate();
	graph.editor->edit(sequence);
	CHECK_FALSE(graph.editor->get_empty_prompt()->is_visible());
	StringName fork = graph.add("SequenceAction");
	StringName wait = graph.add("ActionWaitEvent");
	StringName branch = graph.add("ActionBranchCondition");
	StringName guard = graph.add("CondStateRange");
	StringName yes = graph.add("ActionSetState");
	StringName no = graph.add("ActionEmitEvent");
	StringName frame = graph.add("GraphFrame");
	GraphFrame *frame_node = Object::cast_to<GraphFrame>(graph.canvas->get_node(NodePath(frame)));
	REQUIRE(frame_node != nullptr);
	LineEdit *frame_title = Object::cast_to<LineEdit>(frame_node->find_children("*", "LineEdit", true, false)[0]);
	REQUIRE(frame_title != nullptr);
	frame_title->set_text("Phase 1 Beat");
	frame_title->emit_signal(SceneStringName(focus_exited));
	MessageQueue::get_singleton()->flush();
	CHECK(String(Dictionary(sequence->get_editor_graph_meta()[frame])["title"]) == "Phase 1 Beat");
	graph.link(fork, 0, wait, 0);
	graph.link(fork, 0, branch, 0);
	graph.link(guard, 0, branch, 1);
	graph.link(branch, 0, yes, 0);
	graph.link(branch, 1, no, 0);
	REQUIRE(sequence->get_steps().size() == 5);
	Ref<SequenceAction> entry = sequence->get_steps()[0];
	CHECK(entry->get_next_steps().size() == 2);
	Ref<ActionBranchCondition> conditional = sequence->get_steps()[2];
	REQUIRE(conditional.is_valid());
	CHECK(conditional->get_true_steps().size() == 1);
	CHECK(conditional->get_false_steps().size() == 1);
	CHECK(conditional->get_condition().is_valid());
	graph.link(no, 0, fork, 0);
	CHECK(Ref<SequenceAction>(sequence->get_steps()[4])->get_next_steps().is_empty());
	graph.link(branch, 1, no, 0, false);
	CHECK(conditional->get_false_steps().is_empty());
	REQUIRE(graph.history.undo());
	MessageQueue::get_singleton()->flush();
	CHECK(conditional->get_false_steps().size() == 1);
	TypedArray<StringName> grouped;
	grouped.push_back(branch);
	graph.canvas->emit_signal("graph_elements_linked_to_frame_request", grouped, frame);
	MessageQueue::get_singleton()->flush();
	Dictionary meta = sequence->get_editor_graph_meta();
	CHECK(StringName(Dictionary(meta[branch])["frame"]) == frame);
	GraphNode *node = Object::cast_to<GraphNode>(graph.canvas->get_node(NodePath(branch)));
	node->set_position_offset(Vector2(543, 321));
	graph.canvas->emit_signal("end_node_move");
	MessageQueue::get_singleton()->flush();
	meta = sequence->get_editor_graph_meta();
	CHECK(Vector2(Dictionary(meta[branch])["position"]) == Vector2(543, 321));
}

TEST_CASE("[SceneTree][SandboxEditor][Gate8] An explicit join waits for both parallel completions and advances once") {
	CanvasFixture graph;
	Ref<SequenceData> sequence;
	sequence.instantiate();
	graph.editor->edit(sequence);
	StringName fork = graph.add("SequenceAction");
	StringName clock = graph.add("SequenceAction");
	StringName event = graph.add("ActionWaitEvent");
	StringName join = graph.add("ActionJoin");
	StringName finish = graph.add("SequenceAction");
	graph.link(fork, 0, clock, 0);
	graph.link(fork, 0, event, 0);
	graph.link(clock, 0, join, 0);
	graph.link(event, 0, join, 0);
	graph.link(join, 0, finish, 0);
	Ref<SequenceAction> timer = sequence->get_steps()[1];
	timer->set_duration_sec(1);
	Ref<ActionWaitEvent> gate = sequence->get_steps()[2];
	gate->set_wait_for_completion(false);
	gate->set_expected_event_id("unlocked");
	Ref<SequenceAction> barrier = sequence->get_steps()[3];
	Ref<SequenceAction> downstream = sequence->get_steps()[4];
	downstream->set_duration_sec(1);
	SequenceDirector *director = memnew(SequenceDirector);
	for (int pass = 0; pass < 2; pass++) {
		director->play_sequence(sequence);
		if (pass == 0) {
			director->step(1);
		} else {
			director->notify_event("unlocked");
		}
		CHECK(director->get_active_step_uuids().has(barrier->get_step_uuid()));
		CHECK_FALSE(director->is_step_completed(barrier->get_step_uuid()));
		CHECK_FALSE(director->get_active_step_uuids().has(downstream->get_step_uuid()));
		if (pass == 0) {
			director->notify_event("unlocked");
		} else {
			director->step(1);
		}
		CHECK(director->is_step_completed(barrier->get_step_uuid()));
		CHECK(director->get_active_step_uuids().count(downstream->get_step_uuid()) == 1);
		director->notify_event("unlocked");
		director->step(1);
		CHECK_FALSE(director->is_playing());
		CHECK(director->is_step_completed(downstream->get_step_uuid()));
	}
	memdelete(director);
}

} // namespace TestSandboxNarrativeGraph
