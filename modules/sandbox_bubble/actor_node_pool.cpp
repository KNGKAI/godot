/**************************************************************************/
/*  actor_node_pool.cpp                                                   */
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

#include "actor_node_pool.h"

#include "hydratable_actor.h"

#include "core/os/thread.h"

Error ActorNodePool::prewarm(const Ref<PackedScene> &p_scene, int p_count, const StringName &p_archetype) {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), ERR_BUSY);
	ERR_FAIL_COND_V(!shells.is_empty(), ERR_ALREADY_IN_USE);
	ERR_FAIL_COND_V(p_scene.is_null() || !p_scene->can_instantiate() || p_count <= 0 || p_archetype == StringName(), ERR_INVALID_PARAMETER);
	available.reserve(p_count);
	shells.reserve(p_count);
	for (int i = 0; i < p_count; i++) {
		Node *instance = p_scene->instantiate();
		Node3D *root = Object::cast_to<Node3D>(instance);
		if (!root) {
			if (instance) {
				memdelete(instance);
			}
			clear();
			return ERR_INVALID_DATA;
		}
		const TypedArray<Node> matches = root->find_children(String(), "HydratableActor", true, false);
		HydratableActor *component = matches.size() == 1 ? Object::cast_to<HydratableActor>(matches[0]) : nullptr;
		if (!component || (component->get_archetype_id() != StringName() && component->get_archetype_id() != p_archetype) || component->get_hydrate_radius() >= component->get_dehydrate_radius()) {
			memdelete(root);
			clear();
			return ERR_INVALID_DATA;
		}
		component->set_archetype_id(p_archetype);
		Shell shell;
		shell.component = component->get_instance_id();
		shell.authored_process_mode = root->get_process_mode();
		shell.authored_visible = root->is_visible();
		root->set_process_mode(Node::PROCESS_MODE_DISABLED);
		root->hide();
		const ObjectID id = root->get_instance_id();
		shells.insert(id, shell);
		available.push_back(id);
	}
	return OK;
}

Node3D *ActorNodePool::claim() {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), nullptr);
	while (!available.is_empty()) {
		const ObjectID id = available[available.size() - 1];
		available.remove_at(available.size() - 1);
		Node3D *root = ObjectDB::get_instance<Node3D>(id);
		if (!root || root->is_queued_for_deletion()) {
			continue;
		}
		Shell &shell = shells[id];
		shell.claimed = true;
		root->set_process_mode(shell.authored_process_mode);
		root->set_visible(shell.authored_visible);
		return root;
	}
	return nullptr;
}

void ActorNodePool::release(Node3D *p_root) {
	ERR_FAIL_COND(!Thread::is_main_thread());
	ERR_FAIL_NULL(p_root);
	const ObjectID id = p_root->get_instance_id();
	Shell *shell = shells.getptr(id);
	ERR_FAIL_COND(!shell || !shell->claimed);
	if (Node *parent = p_root->get_parent()) {
		parent->remove_child(p_root);
		ERR_FAIL_COND(p_root->get_parent());
	}
	p_root->set_process_mode(Node::PROCESS_MODE_DISABLED);
	p_root->hide();
	shell->claimed = false;
	available.push_back(id);
}

HydratableActor *ActorNodePool::get_component(Node3D *p_root) const {
	ERR_FAIL_NULL_V(p_root, nullptr);
	const Shell *shell = shells.getptr(p_root->get_instance_id());
	return shell ? ObjectDB::get_instance<HydratableActor>(shell->component) : nullptr;
}

void ActorNodePool::clear() {
	for (const KeyValue<ObjectID, Shell> &entry : shells) {
		if (Node3D *root = ObjectDB::get_instance<Node3D>(entry.key)) {
			memdelete(root);
		}
	}
	shells.clear();
	available.clear();
}

ActorNodePool::~ActorNodePool() {
	clear();
}
