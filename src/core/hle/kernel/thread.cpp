// Copyright 2014 Citra Emulator Project / PPSSPP Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cinttypes>
#include <optional>
#include <vector>

#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "common/thread_queue_list.h"
#include "core/arm/arm_interface.h"
#include "core/core.h"
#include "core/core_cpu.h"
#include "core/core_timing.h"
#include "core/core_timing_util.h"
#include "core/hle/kernel/errors.h"
#include "core/hle/kernel/handle_table.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/object.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/scheduler.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/result.h"
#include "core/memory.h"

namespace Kernel {

bool Thread::ShouldWait(Thread* thread) const {
    return status != ThreadStatus::Dead;
}

void Thread::Acquire(Thread* thread) {
    ASSERT_MSG(!ShouldWait(thread), "object unavailable!");
}

Thread::Thread(KernelCore& kernel) : WaitObject{kernel} {}
Thread::~Thread() = default;

void Thread::Stop() {
    // Cancel any outstanding wakeup events for this thread
    Core::System::GetInstance().CoreTiming().UnscheduleEvent(kernel.ThreadWakeupCallbackEventType(),
                                                             callback_handle);
    kernel.ThreadWakeupCallbackHandleTable().Close(callback_handle);
    callback_handle = 0;

    // Clean up thread from ready queue
    // This is only needed when the thread is terminated forcefully (SVC TerminateProcess)
    if (status == ThreadStatus::Ready || status == ThreadStatus::Paused) {
        scheduler->UnscheduleThread(this, current_priority);
    }

    status = ThreadStatus::Dead;

    WakeupAllWaitingThreads();

    // Clean up any dangling references in objects that this thread was waiting for
    for (auto& wait_object : wait_objects) {
        wait_object->RemoveWaitingThread(this);
    }
    wait_objects.clear();

    // Mark the TLS slot in the thread's page as free.
    owner_process->FreeTLSSlot(tls_address);
}

void Thread::WakeAfterDelay(s64 nanoseconds) {
    // Don't schedule a wakeup if the thread wants to wait forever
    if (nanoseconds == -1)
        return;

    // This function might be called from any thread so we have to be cautious and use the
    // thread-safe version of ScheduleEvent.
    Core::System::GetInstance().CoreTiming().ScheduleEventThreadsafe(
        Core::Timing::nsToCycles(nanoseconds), kernel.ThreadWakeupCallbackEventType(),
        callback_handle);
}

void Thread::CancelWakeupTimer() {
    Core::System::GetInstance().CoreTiming().UnscheduleEventThreadsafe(
        kernel.ThreadWakeupCallbackEventType(), callback_handle);
}

static std::optional<s32> GetNextProcessorId(u64 mask) {
    for (s32 index = 0; index < Core::NUM_CPU_CORES; ++index) {
        if (mask & (1ULL << index)) {
            if (!Core::System::GetInstance().Scheduler(index).GetCurrentThread()) {
                // Core is enabled and not running any threads, use this one
                return index;
            }
        }
    }
    return {};
}

void Thread::ResumeFromWait() {
    ASSERT_MSG(wait_objects.empty(), "Thread is waking up while waiting for objects");

    switch (status) {
    case ThreadStatus::WaitSynchAll:
    case ThreadStatus::WaitSynchAny:
    case ThreadStatus::WaitHLEEvent:
    case ThreadStatus::WaitSleep:
    case ThreadStatus::WaitIPC:
    case ThreadStatus::WaitMutex:
    case ThreadStatus::WaitArb:
        break;

    case ThreadStatus::Ready:
        // The thread's wakeup callback must have already been cleared when the thread was first
        // awoken.
        ASSERT(wakeup_callback == nullptr);
        // If the thread is waiting on multiple wait objects, it might be awoken more than once
        // before actually resuming. We can ignore subsequent wakeups if the thread status has
        // already been set to ThreadStatus::Ready.
        return;

    case ThreadStatus::Running:
        DEBUG_ASSERT_MSG(false, "Thread with object id {} has already resumed.", GetObjectId());
        return;
    case ThreadStatus::Dead:
        // This should never happen, as threads must complete before being stopped.
        DEBUG_ASSERT_MSG(false, "Thread with object id {} cannot be resumed because it's DEAD.",
                         GetObjectId());
        return;
    }

    wakeup_callback = nullptr;

    if (activity == ThreadActivity::Paused) {
        status = ThreadStatus::Paused;
        return;
    }

    status = ThreadStatus::Ready;

    ChangeScheduler();
}

/**
 * Resets a thread context, making it ready to be scheduled and run by the CPU
 * @param context Thread context to reset
 * @param stack_top Address of the top of the stack
 * @param entry_point Address of entry point for execution
 * @param arg User argument for thread
 */
static void ResetThreadContext(Core::ARM_Interface::ThreadContext& context, VAddr stack_top,
                               VAddr entry_point, u64 arg) {
    context = {};
    context.cpu_registers[0] = arg;
    context.pc = entry_point;
    context.sp = stack_top;
    // TODO(merry): Perform a hardware test to determine the below value.
    // AHP = 0, DN = 1, FTZ = 1, RMode = Round towards zero
    context.fpcr = 0x03C00000;
}

ResultVal<SharedPtr<Thread>> Thread::Create(KernelCore& kernel, std::string name, VAddr entry_point,
                                            u32 priority, u64 arg, s32 processor_id,
                                            VAddr stack_top, Process& owner_process) {
    // Check if priority is in ranged. Lowest priority -> highest priority id.
    if (priority > THREADPRIO_LOWEST) {
        LOG_ERROR(Kernel_SVC, "Invalid thread priority: {}", priority);
        return ERR_INVALID_THREAD_PRIORITY;
    }

    if (processor_id > THREADPROCESSORID_MAX) {
        LOG_ERROR(Kernel_SVC, "Invalid processor id: {}", processor_id);
        return ERR_INVALID_PROCESSOR_ID;
    }

    if (!Memory::IsValidVirtualAddress(owner_process, entry_point)) {
        LOG_ERROR(Kernel_SVC, "(name={}): invalid entry {:016X}", name, entry_point);
        // TODO (bunnei): Find the correct error code to use here
        return ResultCode(-1);
    }

    auto& system = Core::System::GetInstance();
    SharedPtr<Thread> thread(new Thread(kernel));

    thread->thread_id = kernel.CreateNewThreadID();
    thread->status = ThreadStatus::Dormant;
    thread->entry_point = entry_point;
    thread->stack_top = stack_top;
    thread->tpidr_el0 = 0;
    thread->nominal_priority = thread->current_priority = priority;
    thread->last_running_ticks = system.CoreTiming().GetTicks();
    thread->processor_id = processor_id;
    thread->ideal_core = processor_id;
    thread->affinity_mask = 1ULL << processor_id;
    thread->wait_objects.clear();
    thread->mutex_wait_address = 0;
    thread->condvar_wait_address = 0;
    thread->wait_handle = 0;
    thread->name = std::move(name);
    thread->callback_handle = kernel.ThreadWakeupCallbackHandleTable().Create(thread).Unwrap();
    thread->owner_process = &owner_process;
    thread->scheduler = &system.Scheduler(processor_id);
    thread->scheduler->AddThread(thread, priority);
    thread->tls_address = thread->owner_process->MarkNextAvailableTLSSlotAsUsed(*thread);

    // TODO(peachum): move to ScheduleThread() when scheduler is added so selected core is used
    // to initialize the context
    ResetThreadContext(thread->context, stack_top, entry_point, arg);

    return MakeResult<SharedPtr<Thread>>(std::move(thread));
}

void Thread::SetPriority(u32 priority) {
    ASSERT_MSG(priority <= THREADPRIO_LOWEST && priority >= THREADPRIO_HIGHEST,
               "Invalid priority value.");
    nominal_priority = priority;
    UpdatePriority();
}

void Thread::BoostPriority(u32 priority) {
    scheduler->SetThreadPriority(this, priority);
    current_priority = priority;
}

void Thread::SetWaitSynchronizationResult(ResultCode result) {
    context.cpu_registers[0] = result.raw;
}

void Thread::SetWaitSynchronizationOutput(s32 output) {
    context.cpu_registers[1] = output;
}

s32 Thread::GetWaitObjectIndex(WaitObject* object) const {
    ASSERT_MSG(!wait_objects.empty(), "Thread is not waiting for anything");
    auto match = std::find(wait_objects.rbegin(), wait_objects.rend(), object);
    return static_cast<s32>(std::distance(match, wait_objects.rend()) - 1);
}

VAddr Thread::GetCommandBufferAddress() const {
    // Offset from the start of TLS at which the IPC command buffer begins.
    static constexpr int CommandHeaderOffset = 0x80;
    return GetTLSAddress() + CommandHeaderOffset;
}

void Thread::SetStatus(ThreadStatus new_status) {
    if (new_status == status) {
        return;
    }

    if (status == ThreadStatus::Running) {
        last_running_ticks = Core::System::GetInstance().CoreTiming().GetTicks();
    }

    status = new_status;
}

void Thread::AddMutexWaiter(SharedPtr<Thread> thread) {
    if (thread->lock_owner == this) {
        // If the thread is already waiting for this thread to release the mutex, ensure that the
        // waiters list is consistent and return without doing anything.
        const auto iter = std::find(wait_mutex_threads.begin(), wait_mutex_threads.end(), thread);
        ASSERT(iter != wait_mutex_threads.end());
        return;
    }

    // A thread can't wait on two different mutexes at the same time.
    ASSERT(thread->lock_owner == nullptr);

    // Ensure that the thread is not already in the list of mutex waiters
    const auto iter = std::find(wait_mutex_threads.begin(), wait_mutex_threads.end(), thread);
    ASSERT(iter == wait_mutex_threads.end());

    // Keep the list in an ordered fashion
    const auto insertion_point = std::find_if(
        wait_mutex_threads.begin(), wait_mutex_threads.end(),
        [&thread](const auto& entry) { return entry->GetPriority() > thread->GetPriority(); });
    wait_mutex_threads.insert(insertion_point, thread);
    thread->lock_owner = this;

    UpdatePriority();
}

void Thread::RemoveMutexWaiter(SharedPtr<Thread> thread) {
    ASSERT(thread->lock_owner == this);

    // Ensure that the thread is in the list of mutex waiters
    const auto iter = std::find(wait_mutex_threads.begin(), wait_mutex_threads.end(), thread);
    ASSERT(iter != wait_mutex_threads.end());

    wait_mutex_threads.erase(iter);

    thread->lock_owner = nullptr;
    UpdatePriority();
}

void Thread::UpdatePriority() {
    // If any of the threads waiting on the mutex have a higher priority
    // (taking into account priority inheritance), then this thread inherits
    // that thread's priority.
    u32 new_priority = nominal_priority;
    if (!wait_mutex_threads.empty()) {
        if (wait_mutex_threads.front()->current_priority < new_priority) {
            new_priority = wait_mutex_threads.front()->current_priority;
        }
    }

    if (new_priority == current_priority) {
        return;
    }

    scheduler->SetThreadPriority(this, new_priority);
    current_priority = new_priority;

    if (!lock_owner) {
        return;
    }

    // Ensure that the thread is within the correct location in the waiting list.
    auto old_owner = lock_owner;
    lock_owner->RemoveMutexWaiter(this);
    old_owner->AddMutexWaiter(this);

    // Recursively update the priority of the thread that depends on the priority of this one.
    lock_owner->UpdatePriority();
}

void Thread::ChangeCore(u32 core, u64 mask) {
    ideal_core = core;
    affinity_mask = mask;
    ChangeScheduler();
}

void Thread::ChangeScheduler() {
    if (status != ThreadStatus::Ready) {
        return;
    }

    auto& system = Core::System::GetInstance();
    std::optional<s32> new_processor_id{GetNextProcessorId(affinity_mask)};

    if (!new_processor_id) {
        new_processor_id = processor_id;
    }
    if (ideal_core != -1 && system.Scheduler(ideal_core).GetCurrentThread() == nullptr) {
        new_processor_id = ideal_core;
    }

    ASSERT(*new_processor_id < 4);

    // Add thread to new core's scheduler
    auto& next_scheduler = system.Scheduler(*new_processor_id);

    if (*new_processor_id != processor_id) {
        // Remove thread from previous core's scheduler
        scheduler->RemoveThread(this);
        next_scheduler.AddThread(this, current_priority);
    }

    processor_id = *new_processor_id;

    // If the thread was ready, unschedule from the previous core and schedule on the new core
    scheduler->UnscheduleThread(this, current_priority);
    next_scheduler.ScheduleThread(this, current_priority);

    // Change thread's scheduler
    scheduler = &next_scheduler;

    system.CpuCore(processor_id).PrepareReschedule();
}

bool Thread::AllWaitObjectsReady() {
    return std::none_of(
        wait_objects.begin(), wait_objects.end(),
        [this](const SharedPtr<WaitObject>& object) { return object->ShouldWait(this); });
}

bool Thread::InvokeWakeupCallback(ThreadWakeupReason reason, SharedPtr<Thread> thread,
                                  SharedPtr<WaitObject> object, std::size_t index) {
    ASSERT(wakeup_callback);
    return wakeup_callback(reason, std::move(thread), std::move(object), index);
}

void Thread::SetActivity(ThreadActivity value) {
    activity = value;

    if (value == ThreadActivity::Paused) {
        // Set status if not waiting
        if (status == ThreadStatus::Ready) {
            status = ThreadStatus::Paused;
        } else if (status == ThreadStatus::Running) {
            status = ThreadStatus::Paused;
            Core::System::GetInstance().CpuCore(processor_id).PrepareReschedule();
        }
    } else if (status == ThreadStatus::Paused) {
        // Ready to reschedule
        ResumeFromWait();
    }
}

void Thread::Sleep(s64 nanoseconds) {
    // Sleep current thread and check for next thread to schedule
    SetStatus(ThreadStatus::WaitSleep);

    // Create an event to wake the thread up after the specified nanosecond delay has passed
    WakeAfterDelay(nanoseconds);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Gets the current thread
 */
Thread* GetCurrentThread() {
    return Core::System::GetInstance().CurrentScheduler().GetCurrentThread();
}

} // namespace Kernel
