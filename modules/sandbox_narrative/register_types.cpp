#include "register_types.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

#include "modules/sandbox_narrative/actor_registry.h"
#include "modules/sandbox_narrative/mission_data.h"
#include "modules/sandbox_narrative/mission_runner.h"
#include "modules/sandbox_narrative/sandbox_actor.h"
#include "modules/sandbox_narrative/sandbox_condition.h"
#include "modules/sandbox_narrative/sequence_action.h"
#include "modules/sandbox_narrative/sequence_data.h"
#include "modules/sandbox_narrative/sequence_director.h"

static ActorRegistry *actor_registry_singleton = nullptr;
static MissionRunner *mission_runner_singleton = nullptr;

void initialize_sandbox_narrative_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(SandboxActor);
	GDREGISTER_CLASS(ActorRegistry);

	GDREGISTER_CLASS(SandboxCondition);
	GDREGISTER_CLASS(CondStateEquals);
	GDREGISTER_CLASS(CondStateRange);
	GDREGISTER_CLASS(CondEventFired);
	GDREGISTER_CLASS(CondAll);
	GDREGISTER_CLASS(CondAny);
	GDREGISTER_CLASS(CondNot);

	GDREGISTER_CLASS(SequenceAction);
	GDREGISTER_CLASS(ActionJoin);
	GDREGISTER_CLASS(ActionPlayAnimation);
	GDREGISTER_CLASS(ActionSetState);
	GDREGISTER_CLASS(ActionEmitEvent);
	GDREGISTER_CLASS(ActionRunCommand);
	GDREGISTER_CLASS(ActionLockActor);
	GDREGISTER_CLASS(ActionBranchCondition);
	GDREGISTER_CLASS(ActionWaitEvent);
	GDREGISTER_CLASS(ActionBlendCamera);
	GDREGISTER_CLASS(SequenceData);
	GDREGISTER_CLASS(SequenceDirector);

	GDREGISTER_CLASS(ObjectiveData);
	GDREGISTER_CLASS(MissionTransition);
	GDREGISTER_CLASS(MissionData);
	GDREGISTER_CLASS(MissionRunner);

	actor_registry_singleton = memnew(ActorRegistry);
	Engine::get_singleton()->add_singleton(Engine::Singleton("ActorRegistry", ActorRegistry::get_singleton()));

	mission_runner_singleton = memnew(MissionRunner);
	Engine::get_singleton()->add_singleton(Engine::Singleton("MissionRunner", MissionRunner::get_singleton()));
}

void uninitialize_sandbox_narrative_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	if (mission_runner_singleton) {
		Engine::get_singleton()->remove_singleton("MissionRunner");
		memdelete(mission_runner_singleton);
		mission_runner_singleton = nullptr;
	}

	if (actor_registry_singleton) {
		Engine::get_singleton()->remove_singleton("ActorRegistry");
		memdelete(actor_registry_singleton);
		actor_registry_singleton = nullptr;
	}
}
