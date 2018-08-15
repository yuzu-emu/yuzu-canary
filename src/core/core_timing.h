// Copyright 2008 Dolphin Emulator Project / 2017 Citra Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

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

#include <chrono>
#include <functional>
#include <string>
#include "common/common_types.h"

namespace CoreTiming {

struct EventType;

using TimedCallback = std::function<void(u64 userdata, int cycles_late)>;

/**
 * CoreTiming begins at the boundary of timing slice -1. An initial call to Advance() is
 * required to end slice -1 and start slice 0 before the first cycle of code is executed.
 */
void Init();
void Shutdown();

/**
 * This should only be called from the emu thread, if you are calling it any other thread, you are
 * doing something evil
 */
u64 GetTicks();
u64 GetIdleTicks();
void AddTicks(u64 ticks);

/**
 * Returns the event_type identifier. if name is not unique, it will assert.
 */
EventType* RegisterEvent(const std::string& name, TimedCallback callback);
void UnregisterAllEvents();

/**
 * After the first Advance, the slice lengths and the downcount will be reduced whenever an event
 * is scheduled earlier than the current values.
 * Scheduling from a callback will not update the downcount until the Advance() completes.
 */
void ScheduleEvent(s64 cycles_into_future, const EventType* event_type, u64 userdata = 0);

/**
 * This is to be called when outside of hle threads, such as the graphics thread, wants to
 * schedule things to be executed on the main thread.
 * Not that this doesn't change slice_length and thus events scheduled by this might be called
 * with a delay of up to MAX_SLICE_LENGTH
 */
void ScheduleEventThreadsafe(s64 cycles_into_future, const EventType* event_type, u64 userdata);

void UnscheduleEvent(const EventType* event_type, u64 userdata);
void UnscheduleEventThreadsafe(const EventType* event_type, u64 userdata);

/// We only permit one event of each type in the queue at a time.
void RemoveEvent(const EventType* event_type);
void RemoveNormalAndThreadsafeEvent(const EventType* event_type);

/** Advance must be called at the beginning of dispatcher loops, not the end. Advance() ends
 * the previous timing slice and begins the next one, you must Advance from the previous
 * slice to the current one before executing any cycles. CoreTiming starts in slice -1 so an
 * Advance() is required to initialize the slice length before the first cycle of emulated
 * instructions is executed.
 */
void Advance();
void MoveEvents();

/// Pretend that the main CPU has executed enough cycles to reach the next event.
void Idle();

/// Clear all pending events. This should ONLY be done on exit.
void ClearPendingEvents();

void ForceExceptionCheck(s64 cycles);

std::chrono::microseconds GetGlobalTimeUs();

int GetDowncount();

} // namespace CoreTiming
