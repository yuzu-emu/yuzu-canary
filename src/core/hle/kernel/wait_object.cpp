// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/core_cpu.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/object.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/thread.h"

namespace Kernel {

WaitObject::WaitObject(KernelCore& kernel) : Object{kernel} {}
WaitObject::~WaitObject() = default;

void WaitObject::AddWaitingThread(SharedPtr<Thread> thread) {
    auto itr = std::find(waiting_threads.begin(), waiting_threads.end(), thread);
    if (itr == waiting_threads.end())
        waiting_threads.push_back(std::move(thread));
}

void WaitObject::RemoveWaitingThread(Thread* thread) {
    auto itr = std::find(waiting_threads.begin(), waiting_threads.end(), thread);
    // If a thread passed multiple handles to the same object,
    // the kernel might attempt to remove the thread from the object's
    // waiting threads list multiple times.
    if (itr != waiting_threads.end())
        waiting_threads.erase(itr);
}

SharedPtr<Thread> WaitObject::GetHighestPriorityReadyThread() const {
    Thread* candidate = nullptr;
    u32 candidate_priority = THREADPRIO_LOWEST + 1;

    for (const auto& thread : waiting_threads) {
        const ThreadStatus thread_status = thread->GetStatus();

        // The list of waiting threads must not contain threads that are not waiting to be awakened.
        ASSERT_MSG(thread_status == ThreadStatus::WaitSynch ||
                       thread_status == ThreadStatus::WaitHLEEvent,
                   "Inconsistent thread statuses in waiting_threads");

        if (thread->GetPriority() >= candidate_priority)
            continue;

        if (ShouldWait(thread.get()))
            continue;

        candidate = thread.get();
        candidate_priority = thread->GetPriority();
    }

    return candidate;
}

void WaitObject::WakeupWaitingThread(SharedPtr<Thread> thread) {
    ASSERT(!ShouldWait(thread.get()));

    if (!thread) {
        return;
    }

    if (thread->IsSleepingOnWait()) {
        for (const auto& object : thread->GetWaitObjects()) {
            ASSERT(!object->ShouldWait(thread.get()));
            object->Acquire(thread.get());
        }
    } else {
        Acquire(thread.get());
    }

    const std::size_t index = thread->GetWaitObjectIndex(this);

    thread->ClearWaitObjects();

    thread->CancelWakeupTimer();

    bool resume = true;
    if (thread->HasWakeupCallback()) {
        resume = thread->InvokeWakeupCallback(ThreadWakeupReason::Signal, thread, this, index);
    }
    if (resume) {
        thread->ResumeFromWait();
        Core::System::GetInstance().PrepareReschedule(thread->GetProcessorID());
    }
}

void WaitObject::WakeupAllWaitingThreads() {
    while (auto thread = GetHighestPriorityReadyThread()) {
        WakeupWaitingThread(thread);
    }
}

const std::vector<SharedPtr<Thread>>& WaitObject::GetWaitingThreads() const {
    return waiting_threads;
}

} // namespace Kernel
