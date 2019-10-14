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

constexpr int MAX_SLICE_LENGTH = 10000;

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
    downcounts.fill(MAX_SLICE_LENGTH);
    time_slice.fill(MAX_SLICE_LENGTH);
    slice_length = MAX_SLICE_LENGTH;
    global_timer = 0;
    idled_cycles = 0;
    current_context = 0;

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
    ClearPendingEvents();
    UnregisterAllEvents();
}

EventType* CoreTiming::RegisterEvent(const std::string& name, TimedCallback callback) {
    std::lock_guard guard{inner_mutex};
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
    std::lock_guard guard{inner_mutex};
    const s64 timeout = GetTicks() + cycles_into_future;

    // If this event needs to be scheduled before the next advance(), force one early
    if (!is_global_timer_sane) {
        ForceExceptionCheck(cycles_into_future);
    }

    event_queue.emplace_back(Event{timeout, event_fifo_id++, userdata, event_type});
    std::push_heap(event_queue.begin(), event_queue.end(), std::greater<>());
}

void CoreTiming::UnscheduleEvent(const EventType* event_type, u64 userdata) {
    std::lock_guard guard{inner_mutex};
    const auto itr = std::remove_if(event_queue.begin(), event_queue.end(), [&](const Event& e) {
        return e.type == event_type && e.userdata == userdata;
    });

    // Removing random items breaks the invariant so we have to re-establish it.
    if (itr != event_queue.end()) {
        event_queue.erase(itr, event_queue.end());
        std::make_heap(event_queue.begin(), event_queue.end(), std::greater<>());
    }
}

u64 CoreTiming::GetTicks() const {
    u64 ticks = static_cast<u64>(global_timer);
    if (!is_global_timer_sane) {
        ticks += accumulated_ticks;
    }
    return ticks;
}

u64 CoreTiming::GetIdleTicks() const {
    return static_cast<u64>(idled_cycles);
}

void CoreTiming::AddTicks(u64 ticks) {
    accumulated_ticks += ticks;
    downcounts[current_context] -= static_cast<s64>(ticks);
}

void CoreTiming::ClearPendingEvents() {
    event_queue.clear();
}

void CoreTiming::RemoveEvent(const EventType* event_type) {
    std::lock_guard guard{inner_mutex};
    const auto itr = std::remove_if(event_queue.begin(), event_queue.end(),
                                    [&](const Event& e) { return e.type == event_type; });

    // Removing random items breaks the invariant so we have to re-establish it.
    if (itr != event_queue.end()) {
        event_queue.erase(itr, event_queue.end());
        std::make_heap(event_queue.begin(), event_queue.end(), std::greater<>());
    }
}

void CoreTiming::ForceExceptionCheck(s64 cycles) {
    cycles = std::max<s64>(0, cycles);
    if (downcounts[current_context] <= cycles) {
        return;
    }

    // downcount is always (much) smaller than MAX_INT so we can safely cast cycles to an int
    // here. Account for cycles already executed by adjusting the g.slice_length
    downcounts[current_context] = static_cast<int>(cycles);
}

std::optional<u64> CoreTiming::NextAvailableCore(const s64 needed_ticks) const {
    const u64 original_context = current_context;
    u64 next_context = (original_context + 1) % num_cpu_cores;
    while (next_context != original_context) {
        if (time_slice[next_context] >= needed_ticks) {
            return {next_context};
        } else if (time_slice[next_context] >= 0) {
            return std::nullopt;
        }
        next_context = (next_context + 1) % num_cpu_cores;
    }
    return std::nullopt;
}

void CoreTiming::Advance() {
    std::unique_lock<std::mutex> guard(inner_mutex);

    const u64 cycles_executed = accumulated_ticks;
    time_slice[current_context] = std::max<s64>(0, time_slice[current_context] - accumulated_ticks);
    global_timer += cycles_executed;

    is_global_timer_sane = true;

    while (!event_queue.empty() && event_queue.front().time <= global_timer) {
        Event evt = std::move(event_queue.front());
        std::pop_heap(event_queue.begin(), event_queue.end(), std::greater<>());
        event_queue.pop_back();
        inner_mutex.unlock();
        evt.type->callback(evt.userdata, global_timer - evt.time);
        inner_mutex.lock();
    }

    is_global_timer_sane = false;

    // Still events left (scheduled in the future)
    if (!event_queue.empty()) {
        const s64 needed_ticks =
            std::min<s64>(event_queue.front().time - global_timer, MAX_SLICE_LENGTH);
        const auto next_core = NextAvailableCore(needed_ticks);
        if (next_core) {
            downcounts[*next_core] = needed_ticks;
        }
    }

    accumulated_ticks = 0;

    downcounts[current_context] = time_slice[current_context];
}

void CoreTiming::ResetRun() {
    downcounts.fill(MAX_SLICE_LENGTH);
    time_slice.fill(MAX_SLICE_LENGTH);
    current_context = 0;
    // Still events left (scheduled in the future)
    if (!event_queue.empty()) {
        const s64 needed_ticks =
            std::min<s64>(event_queue.front().time - global_timer, MAX_SLICE_LENGTH);
        downcounts[current_context] = needed_ticks;
    }

    is_global_timer_sane = false;
    accumulated_ticks = 0;
}

void CoreTiming::Idle() {
    accumulated_ticks += downcounts[current_context];
    idled_cycles += downcounts[current_context];
    downcounts[current_context] = 0;
}

std::chrono::microseconds CoreTiming::GetGlobalTimeUs() const {
    return std::chrono::microseconds{GetTicks() * 1000000 / BASE_CLOCK_RATE};
}

s64 CoreTiming::GetDowncount() const {
    return downcounts[current_context];
}

} // namespace Core::Timing
