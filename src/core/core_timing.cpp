// Copyright 2008 Dolphin Emulator Project / 2017 Citra Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "core/core_timing.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <tuple>

#include "common/assert.h"
#include "common/thread.h"
#include "core/core_timing_util.h"

namespace Core::Timing {

constexpr int MAX_SLICE_LENGTH = 20000;

struct CoreTiming::Event {
    s64 time;
    u64 fifo_order;
    u64 userdata;
    const EventType* type;

    // Sort by time, unless the times are the same, in which case sort by
    // the order added to the queue
    friend bool operator>(const Event& left, const Event& right) {
        return std::tie(left.time, left.fifo_order) > std::tie(right.time, right.fifo_order);
    }

    friend bool operator<(const Event& left, const Event& right) {
        return std::tie(left.time, left.fifo_order) < std::tie(right.time, right.fifo_order);
    }
};

CoreTiming::CoreTiming() = default;
CoreTiming::~CoreTiming() = default;

void CoreTiming::Initialize() {
    downcount = MAX_SLICE_LENGTH;
    slice_length = MAX_SLICE_LENGTH;
    global_timer = 0;
    idled_cycles = 0;

    // The time between CoreTiming being initialized and the first call to Advance() is considered
    // the slice boundary between slice -1 and slice 0. Dispatcher loops must call Advance() before
    // executing the first cycle of each slice to prepare the slice length and downcount for
    // that slice.
    is_global_timer_sane = true;

    event_fifo_id = 0;

    const auto empty_timed_callback = [](u64, s64) {};
    ev_lost = RegisterEvent("_lost_event", empty_timed_callback);
}

void CoreTiming::Shutdown() {
    MoveEvents();
    ClearPendingEvents();
    UnregisterAllEvents();
}

EventType* CoreTiming::RegisterEvent(const std::string& name, TimedCallback callback) {
    // check for existing type with same name.
    // we want event type names to remain unique so that we can use them for serialization.
    ASSERT_MSG(event_types.find(name) == event_types.end(),
               "CoreTiming Event \"{}\" is already registered. Events should only be registered "
               "during Init to avoid breaking save states.",
               name.c_str());

    auto info = event_types.emplace(name, EventType{callback, nullptr});
    EventType* event_type = &info.first->second;
    event_type->name = &info.first->first;
    return event_type;
}

void CoreTiming::UnregisterAllEvents() {
    ASSERT_MSG(event_queue.empty(), "Cannot unregister events with events pending");
    event_types.clear();
}

void CoreTiming::ScheduleEvent(s64 cycles_into_future, const EventType* event_type, u64 userdata) {
    ASSERT(event_type != nullptr);
    const s64 timeout = GetTicks() + cycles_into_future;

    // If this event needs to be scheduled before the next advance(), force one early
    if (!is_global_timer_sane) {
        ForceExceptionCheck(cycles_into_future);
    }

    event_queue.emplace_back(Event{timeout, event_fifo_id++, userdata, event_type});
    std::push_heap(event_queue.begin(), event_queue.end(), std::greater<>());
}

void CoreTiming::ScheduleEventThreadsafe(s64 cycles_into_future, const EventType* event_type,
                                         u64 userdata) {
    ts_queue.Push(Event{global_timer + cycles_into_future, 0, userdata, event_type});
}

void CoreTiming::UnscheduleEvent(const EventType* event_type, u64 userdata) {
    const auto itr = std::remove_if(event_queue.begin(), event_queue.end(), [&](const Event& e) {
        return e.type == event_type && e.userdata == userdata;
    });

    // Removing random items breaks the invariant so we have to re-establish it.
    if (itr != event_queue.end()) {
        event_queue.erase(itr, event_queue.end());
        std::make_heap(event_queue.begin(), event_queue.end(), std::greater<>());
    }
}

void CoreTiming::UnscheduleEventThreadsafe(const EventType* event_type, u64 userdata) {
    unschedule_queue.Push(std::make_pair(event_type, userdata));
}

u64 CoreTiming::GetTicks() const {
    u64 ticks = static_cast<u64>(global_timer);
    if (!is_global_timer_sane) {
        ticks += slice_length - downcount;
    }
    return ticks;
}

u64 CoreTiming::GetIdleTicks() const {
    return static_cast<u64>(idled_cycles);
}

void CoreTiming::AddTicks(u64 ticks) {
    downcount -= static_cast<int>(ticks);
}

void CoreTiming::ClearPendingEvents() {
    event_queue.clear();
}

void CoreTiming::RemoveEvent(const EventType* event_type) {
    const auto itr = std::remove_if(event_queue.begin(), event_queue.end(),
                                    [&](const Event& e) { return e.type == event_type; });

    // Removing random items breaks the invariant so we have to re-establish it.
    if (itr != event_queue.end()) {
        event_queue.erase(itr, event_queue.end());
        std::make_heap(event_queue.begin(), event_queue.end(), std::greater<>());
    }
}

void CoreTiming::RemoveNormalAndThreadsafeEvent(const EventType* event_type) {
    MoveEvents();
    RemoveEvent(event_type);
}

void CoreTiming::ForceExceptionCheck(s64 cycles) {
    cycles = std::max<s64>(0, cycles);
    if (downcount <= cycles) {
        return;
    }

    // downcount is always (much) smaller than MAX_INT so we can safely cast cycles to an int
    // here. Account for cycles already executed by adjusting the g.slice_length
    slice_length -= downcount - static_cast<int>(cycles);
    downcount = static_cast<int>(cycles);
}

void CoreTiming::MoveEvents() {
    for (Event ev; ts_queue.Pop(ev);) {
        ev.fifo_order = event_fifo_id++;
        event_queue.emplace_back(std::move(ev));
        std::push_heap(event_queue.begin(), event_queue.end(), std::greater<>());
    }
}

void CoreTiming::Advance() {
    MoveEvents();
    for (std::pair<const EventType*, u64> ev; unschedule_queue.Pop(ev);) {
        UnscheduleEvent(ev.first, ev.second);
    }

    const int cycles_executed = slice_length - downcount;
    global_timer += cycles_executed;
    slice_length = MAX_SLICE_LENGTH;

    is_global_timer_sane = true;

    while (!event_queue.empty() && event_queue.front().time <= global_timer) {
        Event evt = std::move(event_queue.front());
        std::pop_heap(event_queue.begin(), event_queue.end(), std::greater<>());
        event_queue.pop_back();
        evt.type->callback(evt.userdata, static_cast<int>(global_timer - evt.time));
    }

    is_global_timer_sane = false;

    // Still events left (scheduled in the future)
    if (!event_queue.empty()) {
        slice_length = static_cast<int>(
            std::min<s64>(event_queue.front().time - global_timer, MAX_SLICE_LENGTH));
    }

    downcount = slice_length;
}

void CoreTiming::Idle() {
    idled_cycles += downcount;
    downcount = 0;
}

std::chrono::microseconds CoreTiming::GetGlobalTimeUs() const {
    return std::chrono::microseconds{GetTicks() * 1000000 / BASE_CLOCK_RATE};
}

int CoreTiming::GetDowncount() const {
    return downcount;
}

} // namespace Core::Timing
