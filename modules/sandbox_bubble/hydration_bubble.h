/**************************************************************************/
/*  hydration_bubble.h                                                    */
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

#include "core/object/object.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "scene/resources/mesh.h"
#include "scene/resources/packed_scene.h"

class Node3D;
class SimulationServer;

class HydrationBubble : public Object {
	GDCLASS(HydrationBubble, Object);

public:
	enum OverflowPolicy {
		KEEP_TIER1_PROXY,
		RECYCLE_FARTHEST_UNPINNED,
	};

private:
	struct Archetype;
	struct ActiveActor {
		ObjectID root;
		Archetype *type = nullptr;
	};
	struct Candidate {
		RID rid;
		double distance_squared = 0;
		bool pinned = false;
		bool visible = false;
		bool operator<(const Candidate &p_other) const;
	};
	static inline HydrationBubble *singleton = nullptr;
	HashMap<StringName, Archetype *> archetypes;
	HashMap<RID, ActiveActor> active;
	ObjectID host;
	bool busy = false;
	bool flushing = false;
	uint64_t hydrations = 0;
	uint64_t dehydrations = 0;
	uint64_t last_transition_usec = 0;
	uint64_t last_update_usec = 0;
	uint64_t last_proxy_usec = 0;

	Dictionary _read_state(RID p_rid) const;
	void _write_state(RID p_rid, const Dictionary &p_state, const Transform3D &p_transform);
	void _hydrate(RID p_rid, Archetype *p_type, Node3D *p_parent);
	void _release(RID p_rid, bool p_write_back);
	void _restore_actor(RID p_rid);
	void _set_hooks(bool p_enabled);
	void _on_host_exiting();
	Variant _cmd_radius(const StringName &p_type, double p_enter, double p_exit);
	Variant _cmd_pin(int64_t p_id);
	Variant _cmd_unpin(int64_t p_id);

protected:
	static void _bind_methods();

public:
	static HydrationBubble *get_singleton() { return singleton; }
	Error configure_archetype(const StringName &p_type, const Ref<PackedScene> &p_scene, int p_capacity, const Ref<Mesh> &p_proxy_mesh, int p_overflow_policy = KEEP_TIER1_PROXY);
	Error set_radii(const StringName &p_type, double p_enter, double p_exit, double p_proxy);
	Error start(Node3D *p_parent);
	void stop();
	void update(double p_delta);
	Dictionary get_stats() const;
	Node3D *get_active_actor(RID p_rid) const;
	void set_entity_pin(RID p_rid, bool p_pinned, const StringName &p_reason);
	void flush_active_actors_to_soa();
	void resync_active_actors_from_soa();

	HydrationBubble();
	~HydrationBubble();
};

VARIANT_ENUM_CAST(HydrationBubble::OverflowPolicy);
