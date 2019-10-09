// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <utility>
#include <vector>

#include "common/assert.h"
#include "core/core.h"
#include "core/core_cpu.h"
#include "core/hle/kernel/errors.h"
#include "core/hle/kernel/handle_table.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/mutex.h"
#include "core/hle/kernel/object.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/scheduler.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/result.h"
#include "core/memory.h"

namespace Kernel {

/// Returns the number of threads that are waiting for a mutex, and the highest priority one among
/// those.
static std::pair<SharedPtr<Thread>, u32> GetHighestPriorityMutexWaitingThread(
    const SharedPtr<Thread>& current_thread, VAddr mutex_addr) {

    SharedPtr<Thread> highest_priority_thread;
    u32 num_waiters = 0;

    for (const auto& thread : current_thread->GetMutexWaitingThreads()) {
        if (thread->GetMutexWaitAddress() != mutex_addr)
            continue;

        ASSERT(thread->GetStatus() == ThreadStatus::WaitMutex);

        ++num_waiters;
        if (highest_priority_thread == nullptr ||
            thread->GetPriority() < highest_priority_thread->GetPriority()) {
            highest_priority_thread = thread;
        }
    }

    return {highest_priority_thread, num_waiters};
}

/// Update the mutex owner field of all threads waiting on the mutex to point to the new owner.
static void TransferMutexOwnership(VAddr mutex_addr, SharedPtr<Thread> current_thread,
                                   SharedPtr<Thread> new_owner) {
    const auto threads = current_thread->GetMutexWaitingThreads();
    for (const auto& thread : threads) {
        if (thread->GetMutexWaitAddress() != mutex_addr)
            continue;

        ASSERT(thread->GetLockOwner() == current_thread);
        current_thread->RemoveMutexWaiter(thread);
        if (new_owner != thread)
            new_owner->AddMutexWaiter(thread);
    }
}

Mutex::Mutex(Core::System& system) : system{system} {}
Mutex::~Mutex() = default;

ResultCode Mutex::TryAcquire(VAddr address, Handle holding_thread_handle,
                             Handle requesting_thread_handle) {
    // The mutex address must be 4-byte aligned
    if ((address % sizeof(u32)) != 0) {
        return ERR_INVALID_ADDRESS;
    }

    const auto& handle_table = system.Kernel().CurrentProcess()->GetHandleTable();
    Thread* const current_thread = system.CurrentScheduler().GetCurrentThread();
    SharedPtr<Thread> holding_thread = handle_table.Get<Thread>(holding_thread_handle);
    SharedPtr<Thread> requesting_thread = handle_table.Get<Thread>(requesting_thread_handle);

    // TODO(Subv): It is currently unknown if it is possible to lock a mutex in behalf of another
    // thread.
    ASSERT(requesting_thread == current_thread);

    u32 addr_value = Memory::Read32(address);

    // If the mutex isn't being held, just return success.
    if (addr_value != (holding_thread_handle | Mutex::MutexHasWaitersFlag)) {
        return RESULT_SUCCESS;
    }

    if (holding_thread == nullptr) {
        return ERR_INVALID_HANDLE;
    }

    // This a workaround where an unknown bug writes the mutex value to give ownership to a cond var
    // waiting thread.
    if (holding_thread->GetStatus() == ThreadStatus::WaitCondVar) {
        if (holding_thread->GetMutexWaitAddress() == address) {
            Release(address, holding_thread.get());
            addr_value = Memory::Read32(address);
            if (addr_value == 0)
                return RESULT_SUCCESS;
            else {
                holding_thread = handle_table.Get<Thread>(addr_value & Mutex::MutexOwnerMask);
            }
        }
    }

    // Wait until the mutex is released
    current_thread->SetMutexWaitAddress(address);
    current_thread->SetWaitHandle(requesting_thread_handle);

    current_thread->SetStatus(ThreadStatus::WaitMutex);
    current_thread->InvalidateWakeupCallback();

    // Update the lock holder thread's priority to prevent priority inversion.
    holding_thread->AddMutexWaiter(current_thread);

    system.PrepareReschedule();

    return RESULT_SUCCESS;
}

ResultCode Mutex::Release(VAddr address, Thread* holding_thread) {
    // The mutex address must be 4-byte aligned
    if ((address % sizeof(u32)) != 0) {
        return ERR_INVALID_ADDRESS;
    }

    auto [thread, num_waiters] = GetHighestPriorityMutexWaitingThread(holding_thread, address);

    // There are no more threads waiting for the mutex, release it completely.
    if (thread == nullptr) {
        Memory::Write32(address, 0);
        return RESULT_SUCCESS;
    }

    // Transfer the ownership of the mutex from the previous owner to the new one.
    TransferMutexOwnership(address, holding_thread, thread);

    u32 mutex_value = thread->GetWaitHandle();

    if (num_waiters >= 2) {
        // Notify the guest that there are still some threads waiting for the mutex
        mutex_value |= Mutex::MutexHasWaitersFlag;
    }

    // Grant the mutex to the next waiting thread and resume it.
    Memory::Write32(address, mutex_value);

    ASSERT(thread->GetStatus() == ThreadStatus::WaitMutex);
    thread->ResumeFromWait();

    thread->SetLockOwner(nullptr);
    thread->SetCondVarWaitAddress(0);
    thread->SetMutexWaitAddress(0);
    thread->SetWaitHandle(0);

    if (thread->GetProcessorID() >= 0)
        system.CpuCore(thread->GetProcessorID()).PrepareReschedule();
    if (holding_thread->GetProcessorID() >= 0)
        system.CpuCore(holding_thread->GetProcessorID()).PrepareReschedule();

    return RESULT_SUCCESS;
}
} // namespace Kernel
