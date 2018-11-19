// Copyright 2014 Citra Emulator Project / PPSSPP Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cinttypes>
#include <optional>
#include <vector>

#include <boost/range/algorithm_ext/erase.hpp>

#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "common/math_util.h"
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
    CoreTiming::UnscheduleEvent(kernel.ThreadWakeupCallbackEventType(), callback_handle);
    kernel.ThreadWakeupCallbackHandleTable().Close(callback_handle);
    callback_handle = 0;

    // Clean up thread from ready queue
    // This is only needed when the thread is terminated forcefully (SVC TerminateProcess)
    if (status == ThreadStatus::Ready) {
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

void WaitCurrentThread_Sleep() {
    Thread* thread = GetCurrentThread();
    thread->SetStatus(ThreadStatus::WaitSleep);
}

void ExitCurrentThread() {
    Thread* thread = GetCurrentThread();
    thread->Stop();
    Core::System::GetInstance().CurrentScheduler().RemoveThread(thread);
}

void Thread::WakeAfterDelay(s64 nanoseconds) {
    // Don't schedule a wakeup if the thread wants to wait forever
    if (nanoseconds == -1)
        return;

    // This function might be called from any thread so we have to be cautious and use the
    // thread-safe version of ScheduleEvent.
    CoreTiming::ScheduleEventThreadsafe(CoreTiming::nsToCycles(nanoseconds),
                                        kernel.ThreadWakeupCallbackEventType(), callback_handle);
}

void Thread::CancelWakeupTimer() {
    CoreTiming::UnscheduleEventThreadsafe(kernel.ThreadWakeupCallbackEventType(), callback_handle);
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

    // TODO(yuriks): Other checks, returning 0xD9001BEA

    if (!Memory::IsValidVirtualAddress(owner_process, entry_point)) {
        LOG_ERROR(Kernel_SVC, "(name={}): invalid entry {:016X}", name, entry_point);
        // TODO (bunnei): Find the correct error code to use here
        return ResultCode(-1);
    }

    SharedPtr<Thread> thread(new Thread(kernel));

    thread->thread_id = kernel.CreateNewThreadID();
    thread->status = ThreadStatus::Dormant;
    thread->entry_point = entry_point;
    thread->stack_top = stack_top;
    thread->tpidr_el0 = 0;
    thread->nominal_priority = thread->current_priority = priority;
    thread->last_running_ticks = CoreTiming::GetTicks();
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
    thread->scheduler = &Core::System::GetInstance().Scheduler(processor_id);
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

SharedPtr<Thread> SetupMainThread(KernelCore& kernel, VAddr entry_point, u32 priority,
                                  Process& owner_process) {
    // Setup page table so we can write to memory
    SetCurrentPageTable(&owner_process.VMManager().page_table);

    // Initialize new "main" thread
    const VAddr stack_top = owner_process.VMManager().GetTLSIORegionEndAddress();
    auto thread_res = Thread::Create(kernel, "main", entry_point, priority, 0, THREADPROCESSORID_0,
                                     stack_top, owner_process);

    SharedPtr<Thread> thread = std::move(thread_res).Unwrap();

    // Register 1 must be a handle to the main thread
    const Handle guest_handle = owner_process.GetHandleTable().Create(thread).Unwrap();
    thread->SetGuestHandle(guest_handle);
    thread->GetContext().cpu_registers[1] = guest_handle;

    // Threads by default are dormant, wake up the main thread so it runs when the scheduler fires
    thread->ResumeFromWait();

    return thread;
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
        last_running_ticks = CoreTiming::GetTicks();
    }

    status = new_status;
}

void Thread::AddMutexWaiter(SharedPtr<Thread> thread) {
    if (thread->lock_owner == this) {
        // If the thread is already waiting for this thread to release the mutex, ensure that the
        // waiters list is consistent and return without doing anything.
        auto itr = std::find(wait_mutex_threads.begin(), wait_mutex_threads.end(), thread);
        ASSERT(itr != wait_mutex_threads.end());
        return;
    }

    // A thread can't wait on two different mutexes at the same time.
    ASSERT(thread->lock_owner == nullptr);

    // Ensure that the thread is not already in the list of mutex waiters
    auto itr = std::find(wait_mutex_threads.begin(), wait_mutex_threads.end(), thread);
    ASSERT(itr == wait_mutex_threads.end());

    thread->lock_owner = this;
    wait_mutex_threads.emplace_back(std::move(thread));
    UpdatePriority();
}

void Thread::RemoveMutexWaiter(SharedPtr<Thread> thread) {
    ASSERT(thread->lock_owner == this);

    // Ensure that the thread is in the list of mutex waiters
    auto itr = std::find(wait_mutex_threads.begin(), wait_mutex_threads.end(), thread);
    ASSERT(itr != wait_mutex_threads.end());

    boost::remove_erase(wait_mutex_threads, thread);
    thread->lock_owner = nullptr;
    UpdatePriority();
}

void Thread::UpdatePriority() {
    // Find the highest priority among all the threads that are waiting for this thread's lock
    u32 new_priority = nominal_priority;
    for (const auto& thread : wait_mutex_threads) {
        if (thread->nominal_priority < new_priority)
            new_priority = thread->nominal_priority;
    }

    if (new_priority == current_priority)
        return;

    scheduler->SetThreadPriority(this, new_priority);

    current_priority = new_priority;

    // Recursively update the priority of the thread that depends on the priority of this one.
    if (lock_owner)
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

void Thread::YieldNormal() {
    // Avoid yielding if the thread isn't even running.
    if (status != ThreadStatus::Running) {
        return;
    }

    if (nominal_priority < THREADPRIO_COUNT) {
        scheduler->RescheduleThread(this, nominal_priority);
        scheduler->Reschedule();
    }
}

void Thread::YieldWithLoadBalancing() {
    auto priority = nominal_priority;
    auto core = processor_id;

    // Avoid yielding if the thread isn't even running.
    if (status != ThreadStatus::Running) {
        Core::System::GetInstance().CpuCore(processor_id).PrepareReschedule();
        return;
    }

    SharedPtr<Thread> next;
    const auto& threads = scheduler->GetThreadList();

    if (priority < THREADPRIO_COUNT) {
        // Reschedule thread to end of queue.
        scheduler->RescheduleThread(this, priority);

        const auto iter = std::find_if(threads.begin(), threads.end(),
                                       [&priority](const SharedPtr<Thread>& thread) {
                                           return thread->GetNominalPriority() == priority;
                                       });

        if (iter != threads.end())
            next = iter->get();
    }

    Thread* suggested_thread = nullptr;

    for (int i = 0; i < 4; ++i) {
        if (i == core)
            continue;

        const auto res =
            Core::System::GetInstance().CpuCore(i).Scheduler().GetNextSuggestedThread(core);
        if (res != nullptr) {
            suggested_thread = res;
            break;
        }
    }

    if (suggested_thread != nullptr)
        suggested_thread->ChangeCore(core, suggested_thread->GetAffinityMask());
}

void Thread::YieldAndWaitForLoadBalancing() {
    UNIMPLEMENTED_MSG("Wait for load balancing thread yield type is not implemented!");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Gets the current thread
 */
Thread* GetCurrentThread() {
    return Core::System::GetInstance().CurrentScheduler().GetCurrentThread();
}

} // namespace Kernel
