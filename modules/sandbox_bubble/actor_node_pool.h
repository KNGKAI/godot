/**************************************************************************/
/*  actor_node_pool.h                                                     */
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

#pragma once

#include "core/templates/a_hash_map.h"
#include "core/templates/local_vector.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/packed_scene.h"

class HydratableActor;

class ActorNodePool {
	struct Shell {
		ObjectID component;
		Node::ProcessMode authored_process_mode = Node::PROCESS_MODE_INHERIT;
		bool authored_visible = true;
		bool claimed = false;
	};

	AHashMap<ObjectID, Shell> shells;
	LocalVector<ObjectID> available;
	void clear();

public:
	Error prewarm(const Ref<PackedScene> &p_scene, int p_count, const StringName &p_archetype);
	Node3D *claim();
	void release(Node3D *p_root);
	int get_available() const { return available.size(); }
	HydratableActor *get_component(Node3D *p_root) const;

	ActorNodePool() = default;
	ActorNodePool(const ActorNodePool &) = delete;
	ActorNodePool &operator=(const ActorNodePool &) = delete;
	~ActorNodePool();
};
