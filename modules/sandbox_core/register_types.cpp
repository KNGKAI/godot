/**************************************************************************/
/*  register_types.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "register_types.h"

#include "command_bus.h"
#include "event_bus.h"
#include "sandbox_clock.h"
#include "sandbox_debug_server.h"
#include "sandbox_schema_registry.h"
#include "sandbox_serializer.h"
#include "world_state.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/object/class_db.h"

static SandboxClock *sandbox_clock_singleton = nullptr;
static WorldState *world_state_singleton = nullptr;
static EventBus *event_bus_singleton = nullptr;
static CommandBus *command_bus_singleton = nullptr;
static SandboxSerializer *sandbox_serializer_singleton = nullptr;
static SandboxDebugServer *sandbox_debug_server_singleton = nullptr;

void initialize_sandbox_core_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(SandboxClock);
	GDREGISTER_CLASS(WorldState);
	GDREGISTER_CLASS(SandboxSchemaRegistry);
	GDREGISTER_CLASS(EventBus);
	GDREGISTER_CLASS(CommandBus);
	GDREGISTER_CLASS(SandboxSerializer);
	GDREGISTER_CLASS(SandboxDebugServer);

	GLOBAL_DEF(PropertyInfo(Variant::STRING, "sandbox/schema/registry_path", PROPERTY_HINT_FILE, "*.tres,*.res"), "");
	GLOBAL_DEF(PropertyInfo(Variant::BOOL, "sandbox/schema/strict_mode"), false);

	sandbox_clock_singleton = memnew(SandboxClock);
	world_state_singleton = memnew(WorldState);
	event_bus_singleton = memnew(EventBus);
	command_bus_singleton = memnew(CommandBus);
	sandbox_serializer_singleton = memnew(SandboxSerializer);
	sandbox_debug_server_singleton = memnew(SandboxDebugServer);

	Engine::get_singleton()->add_singleton(Engine::Singleton("SandboxClock", sandbox_clock_singleton));
	Engine::get_singleton()->add_singleton(Engine::Singleton("WorldState", world_state_singleton));
	Engine::get_singleton()->add_singleton(Engine::Singleton("EventBus", event_bus_singleton));
	Engine::get_singleton()->add_singleton(Engine::Singleton("CommandBus", command_bus_singleton));
	Engine::get_singleton()->add_singleton(Engine::Singleton("SandboxSerializer", sandbox_serializer_singleton));
	Engine::get_singleton()->add_singleton(Engine::Singleton("SandboxDebugServer", sandbox_debug_server_singleton));
}

void uninitialize_sandbox_core_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	SandboxSchemaRegistry::cleanup_active_registry();

	if (Engine::get_singleton()) {
		Engine::get_singleton()->remove_singleton("SandboxDebugServer");
		Engine::get_singleton()->remove_singleton("SandboxSerializer");
		Engine::get_singleton()->remove_singleton("CommandBus");
		Engine::get_singleton()->remove_singleton("EventBus");
		Engine::get_singleton()->remove_singleton("WorldState");
		Engine::get_singleton()->remove_singleton("SandboxClock");
	}

	if (sandbox_debug_server_singleton) {
		memdelete(sandbox_debug_server_singleton);
		sandbox_debug_server_singleton = nullptr;
	}
	if (sandbox_serializer_singleton) {
		memdelete(sandbox_serializer_singleton);
		sandbox_serializer_singleton = nullptr;
	}
	if (command_bus_singleton) {
		memdelete(command_bus_singleton);
		command_bus_singleton = nullptr;
	}
	if (event_bus_singleton) {
		memdelete(event_bus_singleton);
		event_bus_singleton = nullptr;
	}
	if (world_state_singleton) {
		memdelete(world_state_singleton);
		world_state_singleton = nullptr;
	}
	if (sandbox_clock_singleton) {
		memdelete(sandbox_clock_singleton);
		sandbox_clock_singleton = nullptr;
	}
}
