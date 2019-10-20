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

bool Thread::ShouldWait(const Thread* thread) const {
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
    SetStatus(ThreadStatus::Dead);
    WakeupAllWaitingThreads();

    // Clean up any dangling references in objects that this thread was waiting for
    for (auto& wait_object : wait_objects) {
        wait_object->RemoveWaitingThread(this);
    }
    wait_objects.clear();

    owner_process->UnregisterThread(this);

    // Mark the TLS slot in the thread's page as free.
    owner_process->FreeTLSRegion(tls_address);
}

void Thread::WakeAfterDelay(s64 nanoseconds) {
    // Don't schedule a wakeup if the thread wants to wait forever
    if (nanoseconds == -1)
        return;

    // This function might be called from any thread so we have to be cautious and use the
    // thread-safe version of ScheduleEvent.
    const s64 cycles = Core::Timing::nsToCycles(std::chrono::nanoseconds{nanoseconds});
    Core::System::GetInstance().CoreTiming().ScheduleEvent(
        cycles, kernel.ThreadWakeupCallbackEventType(), callback_handle);
}

void Thread::CancelWakeupTimer() {
    Core::System::GetInstance().CoreTiming().UnscheduleEvent(kernel.ThreadWakeupCallbackEventType(),
                                                             callback_handle);
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
    case ThreadStatus::WaitSynch:
    case ThreadStatus::WaitHLEEvent:
    case ThreadStatus::WaitSleep:
    case ThreadStatus::WaitIPC:
    case ThreadStatus::WaitMutex:
    case ThreadStatus::WaitCondVar:
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
        SetStatus(ThreadStatus::Paused);
        return;
    }

    SetStatus(ThreadStatus::Ready);
}

void Thread::CancelWait() {
    ASSERT(GetStatus() == ThreadStatus::WaitSynch);
    ClearWaitObjects();
    SetWaitSynchronizationResult(ERR_SYNCHRONIZATION_CANCELED);
    ResumeFromWait();
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
    auto& scheduler = kernel.GlobalScheduler();
    scheduler.AddThread(thread);
    thread->tls_address = thread->owner_process->CreateTLSRegion();

    thread->owner_process->RegisterThread(thread.get());

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

void Thread::SetWaitSynchronizationResult(ResultCode result) {
    context.cpu_registers[0] = result.raw;
}

void Thread::SetWaitSynchronizationOutput(s32 output) {
    context.cpu_registers[1] = output;
}

s32 Thread::GetWaitObjectIndex(const WaitObject* object) const {
    ASSERT_MSG(!wait_objects.empty(), "Thread is not waiting for anything");
    const auto match = std::find(wait_objects.rbegin(), wait_objects.rend(), object);
    return static_cast<s32>(std::distance(match, wait_objects.rend()) - 1);
}

VAddr Thread::GetCommandBufferAddress() const {
    // Offset from the start of TLS at which the IPC command buffer begins.
    constexpr u64 command_header_offset = 0x80;
    return GetTLSAddress() + command_header_offset;
}

void Thread::SetStatus(ThreadStatus new_status) {
    if (new_status == status) {
        return;
    }

    switch (new_status) {
    case ThreadStatus::Ready:
    case ThreadStatus::Running:
        SetSchedulingStatus(ThreadSchedStatus::Runnable);
        break;
    case ThreadStatus::Dormant:
        SetSchedulingStatus(ThreadSchedStatus::None);
        break;
    case ThreadStatus::Dead:
        SetSchedulingStatus(ThreadSchedStatus::Exited);
        break;
    default:
        SetSchedulingStatus(ThreadSchedStatus::Paused);
        break;
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

    SetCurrentPriority(new_priority);

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
    SetCoreAndAffinityMask(core, mask);
}

bool Thread::AllWaitObjectsReady() const {
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
        if (status == ThreadStatus::Ready || status == ThreadStatus::Running) {
            SetStatus(ThreadStatus::Paused);
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

bool Thread::YieldSimple() {
    auto& scheduler = kernel.GlobalScheduler();
    return scheduler.YieldThread(this);
}

bool Thread::YieldAndBalanceLoad() {
    auto& scheduler = kernel.GlobalScheduler();
    return scheduler.YieldThreadAndBalanceLoad(this);
}

bool Thread::YieldAndWaitForLoadBalancing() {
    auto& scheduler = kernel.GlobalScheduler();
    return scheduler.YieldThreadAndWaitForLoadBalancing(this);
}

void Thread::SetSchedulingStatus(ThreadSchedStatus new_status) {
    const u32 old_flags = scheduling_state;
    scheduling_state = (scheduling_state & static_cast<u32>(ThreadSchedMasks::HighMask)) |
                       static_cast<u32>(new_status);
    AdjustSchedulingOnStatus(old_flags);
}

void Thread::SetCurrentPriority(u32 new_priority) {
    const u32 old_priority = std::exchange(current_priority, new_priority);
    AdjustSchedulingOnPriority(old_priority);
}

ResultCode Thread::SetCoreAndAffinityMask(s32 new_core, u64 new_affinity_mask) {
    const auto HighestSetCore = [](u64 mask, u32 max_cores) {
        for (s32 core = max_cores - 1; core >= 0; core--) {
            if (((mask >> core) & 1) != 0) {
                return core;
            }
        }
        return -1;
    };

    const bool use_override = affinity_override_count != 0;
    if (new_core == THREADPROCESSORID_DONT_UPDATE) {
        new_core = use_override ? ideal_core_override : ideal_core;
        if ((new_affinity_mask & (1ULL << new_core)) == 0) {
            return ERR_INVALID_COMBINATION;
        }
    }
    if (use_override) {
        ideal_core_override = new_core;
        affinity_mask_override = new_affinity_mask;
    } else {
        const u64 old_affinity_mask = std::exchange(affinity_mask, new_affinity_mask);
        ideal_core = new_core;
        if (old_affinity_mask != new_affinity_mask) {
            const s32 old_core = processor_id;
            if (processor_id >= 0 && ((affinity_mask >> processor_id) & 1) == 0) {
                if (ideal_core < 0) {
                    processor_id = HighestSetCore(affinity_mask, GlobalScheduler::NUM_CPU_CORES);
                } else {
                    processor_id = ideal_core;
                }
            }
            AdjustSchedulingOnAffinity(old_affinity_mask, old_core);
        }
    }
    return RESULT_SUCCESS;
}

void Thread::AdjustSchedulingOnStatus(u32 old_flags) {
    if (old_flags == scheduling_state) {
        return;
    }

    auto& scheduler = kernel.GlobalScheduler();
    if (static_cast<ThreadSchedStatus>(old_flags & static_cast<u32>(ThreadSchedMasks::LowMask)) ==
        ThreadSchedStatus::Runnable) {
        // In this case the thread was running, now it's pausing/exitting
        if (processor_id >= 0) {
            scheduler.Unschedule(current_priority, processor_id, this);
        }

        for (s32 core = 0; core < GlobalScheduler::NUM_CPU_CORES; core++) {
            if (core != processor_id && ((affinity_mask >> core) & 1) != 0) {
                scheduler.Unsuggest(current_priority, static_cast<u32>(core), this);
            }
        }
    } else if (GetSchedulingStatus() == ThreadSchedStatus::Runnable) {
        // The thread is now set to running from being stopped
        if (processor_id >= 0) {
            scheduler.Schedule(current_priority, processor_id, this);
        }

        for (s32 core = 0; core < GlobalScheduler::NUM_CPU_CORES; core++) {
            if (core != processor_id && ((affinity_mask >> core) & 1) != 0) {
                scheduler.Suggest(current_priority, static_cast<u32>(core), this);
            }
        }
    }

    scheduler.SetReselectionPending();
}

void Thread::AdjustSchedulingOnPriority(u32 old_priority) {
    if (GetSchedulingStatus() != ThreadSchedStatus::Runnable) {
        return;
    }
    auto& scheduler = Core::System::GetInstance().GlobalScheduler();
    if (processor_id >= 0) {
        scheduler.Unschedule(old_priority, processor_id, this);
    }

    for (u32 core = 0; core < GlobalScheduler::NUM_CPU_CORES; core++) {
        if (core != processor_id && ((affinity_mask >> core) & 1) != 0) {
            scheduler.Unsuggest(old_priority, core, this);
        }
    }

    // Add thread to the new priority queues.
    Thread* current_thread = GetCurrentThread();

    if (processor_id >= 0) {
        if (current_thread == this) {
            scheduler.SchedulePrepend(current_priority, processor_id, this);
        } else {
            scheduler.Schedule(current_priority, processor_id, this);
        }
    }

    for (u32 core = 0; core < GlobalScheduler::NUM_CPU_CORES; core++) {
        if (core != processor_id && ((affinity_mask >> core) & 1) != 0) {
            scheduler.Suggest(current_priority, core, this);
        }
    }

    scheduler.SetReselectionPending();
}

void Thread::AdjustSchedulingOnAffinity(u64 old_affinity_mask, s32 old_core) {
    auto& scheduler = Core::System::GetInstance().GlobalScheduler();
    if (GetSchedulingStatus() != ThreadSchedStatus::Runnable ||
        current_priority >= THREADPRIO_COUNT) {
        return;
    }

    for (u32 core = 0; core < GlobalScheduler::NUM_CPU_CORES; core++) {
        if (((old_affinity_mask >> core) & 1) != 0) {
            if (core == old_core) {
                scheduler.Unschedule(current_priority, core, this);
            } else {
                scheduler.Unsuggest(current_priority, core, this);
            }
        }
    }

    for (u32 core = 0; core < GlobalScheduler::NUM_CPU_CORES; core++) {
        if (((affinity_mask >> core) & 1) != 0) {
            if (core == processor_id) {
                scheduler.Schedule(current_priority, core, this);
            } else {
                scheduler.Suggest(current_priority, core, this);
            }
        }
    }

    scheduler.SetReselectionPending();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Gets the current thread
 */
Thread* GetCurrentThread() {
    return Core::System::GetInstance().CurrentScheduler().GetCurrentThread();
}

} // namespace Kernel
