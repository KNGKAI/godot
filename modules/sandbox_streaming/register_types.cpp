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

#include "cell_residency.h"
#include "origin_shifter.h"
#include "streaming_invoker.h"
#include "streaming_server.h"
#include "world_partition_manifest.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

void initialize_sandbox_streaming_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(WorldPartitionLayer);
	GDREGISTER_CLASS(WorldPartitionCell);
	GDREGISTER_CLASS(WorldPartitionManifest);
	GDREGISTER_CLASS(CellSceneBundle);
	GDREGISTER_CLASS(StreamingInvoker);
	GDREGISTER_ABSTRACT_CLASS(OriginShifter);
	GDREGISTER_ABSTRACT_CLASS(StreamingServer);

	OriginShifter *shifter = memnew(OriginShifter);
	StreamingServer *streaming = memnew(StreamingServer);
	Engine::get_singleton()->add_singleton(Engine::Singleton("OriginShifter", shifter));
	Engine::get_singleton()->add_singleton(Engine::Singleton("StreamingServer", streaming));
}

void uninitialize_sandbox_streaming_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	Engine::get_singleton()->remove_singleton("StreamingServer");
	memdelete(StreamingServer::get_singleton());
	Engine::get_singleton()->remove_singleton("OriginShifter");
	memdelete(OriginShifter::get_singleton());
}
