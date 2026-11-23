/**************************************************************************/
/*  event_bus.h                                                           */
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

#include "core/object/class_db.h"
#include "core/object/object.h"
#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/variant/callable.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"

class EventBus : public Object {
	GDCLASS(EventBus, Object);

public:
	struct EventRecord {
		uint64_t timestamp_usec = 0;
		StringName event_id;
		Dictionary payload;
	};

	typedef void (*NativeEventObserver)(const StringName &p_event_id, const Dictionary &p_payload);

private:
	static inline EventBus *singleton = nullptr;

	Mutex deferred_mutex;
	LocalVector<EventRecord> deferred_queue;

	HashMap<StringName, LocalVector<Callable>> subscribers;
	LocalVector<NativeEventObserver> native_observers;

	LocalVector<EventRecord> history_ring;
	uint32_t history_capacity = 256;
	uint32_t history_write_cursor = 0;

	void _dispatch_on_main_thread(const StringName &p_event_id, const Dictionary &p_payload, uint64_t p_timestamp_usec);
	void _record_history(const StringName &p_event_id, const Dictionary &p_payload, uint64_t p_timestamp_usec);

protected:
	static void _bind_methods();

public:
	static EventBus *get_singleton() { return singleton; }

	void subscribe(const StringName &p_event_id, const Callable &p_callable);
	void unsubscribe(const StringName &p_event_id, const Callable &p_callable);
	bool has_subscribers(const StringName &p_event_id) const;

	void add_native_observer(NativeEventObserver p_observer);
	void remove_native_observer(NativeEventObserver p_observer);

	// Thread-aware emit: automatically routes off-main-thread calls into dispatch_deferred (Finding 5 & Invariant #4).
	void emit(const StringName &p_event_id, const Dictionary &p_payload = Dictionary());
	void dispatch_deferred(const StringName &p_event_id, const Dictionary &p_payload = Dictionary());
	void flush_deferred();
	uint32_t get_pending_deferred_count();

	// Structured history ring buffer for Editor & Terminal inspection
	void set_history_capacity(uint32_t p_capacity);
	uint32_t get_history_capacity() const { return history_capacity; }
	TypedArray<Dictionary> get_history() const;
	void clear_history();
	void clear_all();

	EventBus();
	~EventBus();
};
