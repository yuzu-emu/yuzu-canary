// Copyright 2008 Dolphin Emulator Project / 2017 Citra Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "common/common_types.h"
#include "common/threadsafe_queue.h"

namespace Core::Timing {

/// A callback that may be scheduled for a particular core timing event.
using TimedCallback = std::function<void(u64 userdata, s64 cycles_late)>;

/// Contains the characteristics of a particular event.
struct EventType {
    /// The event's callback function.
    TimedCallback callback;
    /// A pointer to the name of the event.
    const std::string* name;
};

/**
 * This is a system to schedule events into the emulated machine's future. Time is measured
 * in main CPU clock cycles.
 *
 * To schedule an event, you first have to register its type. This is where you pass in the
 * callback. You then schedule events using the type id you get back.
 *
 * The int cyclesLate that the callbacks get is how many cycles late it was.
 * So to schedule a new event on a regular basis:
 * inside callback:
 *   ScheduleEvent(periodInCycles - cyclesLate, callback, "whatever")
 */
class CoreTiming {
public:
    CoreTiming();
    ~CoreTiming();

    CoreTiming(const CoreTiming&) = delete;
    CoreTiming(CoreTiming&&) = delete;

    CoreTiming& operator=(const CoreTiming&) = delete;
    CoreTiming& operator=(CoreTiming&&) = delete;

    /// CoreTiming begins at the boundary of timing slice -1. An initial call to Advance() is
    /// required to end slice - 1 and start slice 0 before the first cycle of code is executed.
    void Initialize();

    /// Tears down all timing related functionality.
    void Shutdown();

    /// Registers a core timing event with the given name and callback.
    ///
    /// @param name     The name of the core timing event to register.
    /// @param callback The callback to execute for the event.
    ///
    /// @returns An EventType instance representing the registered event.
    ///
    /// @pre The name of the event being registered must be unique among all
    ///      registered events.
    ///
    EventType* RegisterEvent(const std::string& name, TimedCallback callback);

    /// Unregisters all registered events thus far. Note: not thread unsafe
    void UnregisterAllEvents();

    /// After the first Advance, the slice lengths and the downcount will be reduced whenever an
    /// event is scheduled earlier than the current values.
    ///
    /// Scheduling from a callback will not update the downcount until the Advance() completes.
    void ScheduleEvent(s64 cycles_into_future, const EventType* event_type, u64 userdata = 0);

    void UnscheduleEvent(const EventType* event_type, u64 userdata);

    /// We only permit one event of each type in the queue at a time.
    void RemoveEvent(const EventType* event_type);

    void ForceExceptionCheck(s64 cycles);

    /// This should only be called from the emu thread, if you are calling it any other thread,
    /// you are doing something evil
    u64 GetTicks() const;

    u64 GetIdleTicks() const;

    void AddTicks(u64 ticks);

    /// Advance must be called at the beginning of dispatcher loops, not the end. Advance() ends
    /// the previous timing slice and begins the next one, you must Advance from the previous
    /// slice to the current one before executing any cycles. CoreTiming starts in slice -1 so an
    /// Advance() is required to initialize the slice length before the first cycle of emulated
    /// instructions is executed.
    void Advance();

    /// Pretend that the main CPU has executed enough cycles to reach the next event.
    void Idle();

    std::chrono::microseconds GetGlobalTimeUs() const;

    void ResetRun();

    s64 GetDowncount() const;

    void SwitchContext(u64 new_context) {
        current_context = new_context;
    }

    bool CanCurrentContextRun() const {
        return time_slice[current_context] > 0;
    }

    std::optional<u64> NextAvailableCore(const s64 needed_ticks) const;

private:
    struct Event;

    /// Clear all pending events. This should ONLY be done on exit.
    void ClearPendingEvents();

    static constexpr u64 num_cpu_cores = 4;

    s64 global_timer = 0;
    s64 idled_cycles = 0;
    s64 slice_length = 0;
    u64 accumulated_ticks = 0;
    std::array<s64, num_cpu_cores> downcounts{};
    // Slice of time assigned to each core per run.
    std::array<s64, num_cpu_cores> time_slice{};
    u64 current_context = 0;

    // Are we in a function that has been called from Advance()
    // If events are scheduled from a function that gets called from Advance(),
    // don't change slice_length and downcount.
    bool is_global_timer_sane = false;

    // The queue is a min-heap using std::make_heap/push_heap/pop_heap.
    // We don't use std::priority_queue because we need to be able to serialize, unserialize and
    // erase arbitrary events (RemoveEvent()) regardless of the queue order. These aren't
    // accomodated by the standard adaptor class.
    std::vector<Event> event_queue;
    u64 event_fifo_id = 0;

    // Stores each element separately as a linked list node so pointers to elements
    // remain stable regardless of rehashes/resizing.
    std::unordered_map<std::string, EventType> event_types;

    EventType* ev_lost = nullptr;

    std::mutex inner_mutex;
};

} // namespace Core::Timing
