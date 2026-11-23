/**************************************************************************/
/*  event_bus.cpp                                                         */
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

#include "event_bus.h"

#include "core/os/os.h"
#include "core/os/thread.h"

void EventBus::subscribe(const StringName &p_event_id, const Callable &p_callable) {
	ERR_FAIL_COND_MSG(!Thread::is_main_thread(), "EventBus::subscribe must be called from the main thread.");
	ERR_FAIL_COND(p_event_id == StringName());
	ERR_FAIL_COND(!p_callable.is_valid());

	LocalVector<Callable> &list = subscribers[p_event_id];
	for (uint32_t i = 0; i < list.size(); i++) {
		if (list[i] == p_callable) {
			return;
		}
	}
	list.push_back(p_callable);
}

void EventBus::unsubscribe(const StringName &p_event_id, const Callable &p_callable) {
	ERR_FAIL_COND_MSG(!Thread::is_main_thread(), "EventBus::unsubscribe must be called from the main thread.");
	HashMap<StringName, LocalVector<Callable>>::Iterator it = subscribers.find(p_event_id);
	if (!it) {
		return;
	}
	LocalVector<Callable> &list = it->value;
	for (uint32_t i = 0; i < list.size(); i++) {
		if (list[i] == p_callable) {
			list.remove_at(i);
			break;
		}
	}
}

bool EventBus::has_subscribers(const StringName &p_event_id) const {
	HashMap<StringName, LocalVector<Callable>>::ConstIterator it = subscribers.find(p_event_id);
	return it && !it->value.is_empty();
}

void EventBus::add_native_observer(NativeEventObserver p_observer) {
	if (!p_observer) {
		return;
	}
	for (uint32_t i = 0; i < native_observers.size(); i++) {
		if (native_observers[i] == p_observer) {
			return;
		}
	}
	native_observers.push_back(p_observer);
}

void EventBus::remove_native_observer(NativeEventObserver p_observer) {
	for (uint32_t i = 0; i < native_observers.size(); i++) {
		if (native_observers[i] == p_observer) {
			native_observers.remove_at(i);
			return;
		}
	}
}

void EventBus::_record_history(const StringName &p_event_id, const Dictionary &p_payload, uint64_t p_timestamp_usec) {
	if (history_capacity == 0) {
		return;
	}
	EventRecord rec;
	rec.timestamp_usec = p_timestamp_usec;
	rec.event_id = p_event_id;
	rec.payload = p_payload;

	if (history_ring.size() < history_capacity) {
		history_ring.push_back(rec);
		history_write_cursor = history_ring.size() % history_capacity;
	} else {
		history_ring[history_write_cursor] = rec;
		history_write_cursor = (history_write_cursor + 1) % history_capacity;
	}
}

void EventBus::_dispatch_on_main_thread(const StringName &p_event_id, const Dictionary &p_payload, uint64_t p_timestamp_usec) {
	_record_history(p_event_id, p_payload, p_timestamp_usec);

	for (uint32_t i = 0; i < native_observers.size(); i++) {
		if (native_observers[i]) {
			native_observers[i](p_event_id, p_payload);
		}
	}

	HashMap<StringName, LocalVector<Callable>>::Iterator it = subscribers.find(p_event_id);
	if (it) {
		// Copy subscriber vector in case a callback modifies subscriptions.
		LocalVector<Callable> call_list(it->value);
		for (uint32_t i = 0; i < call_list.size(); i++) {
			if (call_list[i].is_valid()) {
				Variant arg_id = p_event_id;
				Variant arg_payload = p_payload;
				const Variant *args[2] = { &arg_id, &arg_payload };
				Variant ret;
				Callable::CallError ce;
				call_list[i].callp(args, 2, ret, ce);
				if (ce.error == Callable::CallError::CALL_ERROR_TOO_MANY_ARGUMENTS) {
					// Support 1-arg callbacks taking only payload.
					const Variant *args1[1] = { &arg_payload };
					call_list[i].callp(args1, 1, ret, ce);
					if (ce.error == Callable::CallError::CALL_ERROR_TOO_MANY_ARGUMENTS) {
						call_list[i].callp(nullptr, 0, ret, ce);
					}
				}
			}
		}
	}

	emit_signal(SNAME("event_emitted"), p_event_id, p_payload);
}

void EventBus::emit(const StringName &p_event_id, const Dictionary &p_payload) {
	ERR_FAIL_COND(p_event_id == StringName());
	if (!Thread::is_main_thread()) {
		dispatch_deferred(p_event_id, p_payload);
		return;
	}
	uint64_t now_usec = OS::get_singleton() ? OS::get_singleton()->get_ticks_usec() : 0;
	_dispatch_on_main_thread(p_event_id, p_payload, now_usec);
}

void EventBus::dispatch_deferred(const StringName &p_event_id, const Dictionary &p_payload) {
	ERR_FAIL_COND(p_event_id == StringName());
	EventRecord rec;
	rec.timestamp_usec = OS::get_singleton() ? OS::get_singleton()->get_ticks_usec() : 0;
	rec.event_id = p_event_id;
	rec.payload = p_payload.duplicate(true);

	MutexLock lock(deferred_mutex);
	deferred_queue.push_back(rec);
}

void EventBus::flush_deferred() {
	ERR_FAIL_COND_MSG(!Thread::is_main_thread(), "EventBus::flush_deferred must be called on the main thread.");

	LocalVector<EventRecord> to_dispatch;
	{
		MutexLock lock(deferred_mutex);
		if (deferred_queue.is_empty()) {
			return;
		}
		to_dispatch = std::move(deferred_queue);
		deferred_queue.clear();
	}

	for (uint32_t i = 0; i < to_dispatch.size(); i++) {
		_dispatch_on_main_thread(to_dispatch[i].event_id, to_dispatch[i].payload, to_dispatch[i].timestamp_usec);
	}
}

uint32_t EventBus::get_pending_deferred_count() {
	MutexLock lock(deferred_mutex);
	return deferred_queue.size();
}

void EventBus::set_history_capacity(uint32_t p_capacity) {
	history_capacity = p_capacity;
	clear_history();
}

TypedArray<Dictionary> EventBus::get_history() const {
	TypedArray<Dictionary> out;
	if (history_ring.is_empty()) {
		return out;
	}
	uint32_t count = history_ring.size();
	uint32_t start = (count < history_capacity) ? 0 : history_write_cursor;
	for (uint32_t i = 0; i < count; i++) {
		uint32_t idx = (start + i) % count;
		Dictionary d;
		d["timestamp_usec"] = history_ring[idx].timestamp_usec;
		d["event_id"] = history_ring[idx].event_id;
		d["payload"] = history_ring[idx].payload;
		out.push_back(d);
	}
	return out;
}

void EventBus::clear_history() {
	history_ring.clear();
	history_write_cursor = 0;
}

void EventBus::clear_all() {
	{
		MutexLock lock(deferred_mutex);
		deferred_queue.clear();
	}
	subscribers.clear();
	clear_history();
}

void EventBus::_bind_methods() {
	ClassDB::bind_method(D_METHOD("subscribe", "event_id", "callable"), &EventBus::subscribe);
	ClassDB::bind_method(D_METHOD("unsubscribe", "event_id", "callable"), &EventBus::unsubscribe);
	ClassDB::bind_method(D_METHOD("has_subscribers", "event_id"), &EventBus::has_subscribers);
	ClassDB::bind_method(D_METHOD("emit", "event_id", "payload"), &EventBus::emit, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("dispatch_deferred", "event_id", "payload"), &EventBus::dispatch_deferred, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("flush_deferred"), &EventBus::flush_deferred);
	ClassDB::bind_method(D_METHOD("get_pending_deferred_count"), &EventBus::get_pending_deferred_count);
	ClassDB::bind_method(D_METHOD("set_history_capacity", "capacity"), &EventBus::set_history_capacity);
	ClassDB::bind_method(D_METHOD("get_history_capacity"), &EventBus::get_history_capacity);
	ClassDB::bind_method(D_METHOD("get_history"), &EventBus::get_history);
	ClassDB::bind_method(D_METHOD("clear_history"), &EventBus::clear_history);
	ClassDB::bind_method(D_METHOD("clear_all"), &EventBus::clear_all);

	ADD_SIGNAL(MethodInfo("event_emitted", PropertyInfo(Variant::STRING_NAME, "event_id"), PropertyInfo(Variant::DICTIONARY, "payload")));
}

EventBus::EventBus() {
	singleton = this;
}

EventBus::~EventBus() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
