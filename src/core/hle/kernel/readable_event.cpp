// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include "common/assert.h"
#include "core/hle/kernel/errors.h"
#include "core/hle/kernel/object.h"
#include "core/hle/kernel/readable_event.h"
#include "core/hle/kernel/thread.h"

namespace Kernel {

ReadableEvent::ReadableEvent(KernelCore& kernel) : WaitObject{kernel} {}
ReadableEvent::~ReadableEvent() = default;

bool ReadableEvent::ShouldWait(Thread* thread) const {
    return !signaled;
}

void ReadableEvent::Acquire(Thread* thread) {
    ASSERT_MSG(!ShouldWait(thread), "object unavailable!");

    if (reset_type == ResetType::OneShot)
        signaled = false;
}

void ReadableEvent::Signal() {
    signaled = true;
    WakeupAllWaitingThreads();
}

void ReadableEvent::Clear() {
    signaled = false;
}

ResultCode ReadableEvent::Reset() {
    if (!signaled) {
        // Returning ERR_INVALID_STATE here causes downstream deadlocks, unclear why.
        return RESULT_SUCCESS;
    }

    Clear();

    return RESULT_SUCCESS;
}

void ReadableEvent::WakeupAllWaitingThreads() {
    WaitObject::WakeupAllWaitingThreads();
}

} // namespace Kernel
