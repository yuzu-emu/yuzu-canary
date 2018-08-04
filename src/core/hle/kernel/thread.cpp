// Copyright 2014 Citra Emulator Project / PPSSPP Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cinttypes>
#include <vector>

#include <boost/optional.hpp>
#include <boost/range/algorithm_ext/erase.hpp>

#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "common/math_util.h"
#include "common/thread_queue_list.h"
#include "core/arm/arm_interface.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/core_timing_util.h"
#include "core/hle/kernel/errors.h"
#include "core/hle/kernel/handle_table.h"
#include "core/hle/kernel/object.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/result.h"
#include "core/memory.h"

namespace Kernel {

/// Event type for the thread wake up event
static CoreTiming::EventType* ThreadWakeupEventType = nullptr;

bool Thread::ShouldWait(Thread* thread) const {
    return status != ThreadStatus::Dead;
}

void Thread::Acquire(Thread* thread) {
    ASSERT_MSG(!ShouldWait(thread), "object unavailable!");
}

// TODO(yuriks): This can be removed if Thread objects are explicitly pooled in the future, allowing
//               us to simply use a pool index or similar.
static Kernel::HandleTable wakeup_callback_handle_table;

// The first available thread id at startup
static u32 next_thread_id;

/**
 * Creates a new thread ID
 * @return The new thread ID
 */
inline static u32 const NewThreadId() {
    return next_thread_id++;
}

Thread::Thread() {}
Thread::~Thread() {}

void Thread::Stop() {
    // Cancel any outstanding wakeup events for this thread
    CoreTiming::UnscheduleEvent(ThreadWakeupEventType, callback_handle);
    wakeup_callback_handle_table.Close(callback_handle);
    callback_handle = 0;

    // Clean up thread from ready queue
    // This is only needed when the thread is termintated forcefully (SVC TerminateProcess)
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
    const u64 tls_page = (tls_address - Memory::TLS_AREA_VADDR) / Memory::PAGE_SIZE;
    const u64 tls_slot =
        ((tls_address - Memory::TLS_AREA_VADDR) % Memory::PAGE_SIZE) / Memory::TLS_ENTRY_SIZE;
    Core::CurrentProcess()->tls_slots[tls_page].reset(tls_slot);
}

void WaitCurrentThread_Sleep() {
    Thread* thread = GetCurrentThread();
    thread->status = ThreadStatus::WaitSleep;
}

void ExitCurrentThread() {
    Thread* thread = GetCurrentThread();
    thread->Stop();
    Core::System::GetInstance().CurrentScheduler().RemoveThread(thread);
}

/**
 * Callback that will wake up the thread it was scheduled for
 * @param thread_handle The handle of the thread that's been awoken
 * @param cycles_late The number of CPU cycles that have passed since the desired wakeup time
 */
static void ThreadWakeupCallback(u64 thread_handle, int cycles_late) {
    const auto proper_handle = static_cast<Handle>(thread_handle);
    SharedPtr<Thread> thread = wakeup_callback_handle_table.Get<Thread>(proper_handle);
    if (thread == nullptr) {
        LOG_CRITICAL(Kernel, "Callback fired for invalid thread {:08X}", proper_handle);
        return;
    }

    bool resume = true;

    if (thread->status == ThreadStatus::WaitSynchAny ||
        thread->status == ThreadStatus::WaitSynchAll ||
        thread->status == ThreadStatus::WaitHLEEvent) {
        // Remove the thread from each of its waiting objects' waitlists
        for (auto& object : thread->wait_objects)
            object->RemoveWaitingThread(thread.get());
        thread->wait_objects.clear();

        // Invoke the wakeup callback before clearing the wait objects
        if (thread->wakeup_callback)
            resume = thread->wakeup_callback(ThreadWakeupReason::Timeout, thread, nullptr, 0);
    }

    if (thread->mutex_wait_address != 0 || thread->condvar_wait_address != 0 ||
        thread->wait_handle) {
        ASSERT(thread->status == ThreadStatus::WaitMutex);
        thread->mutex_wait_address = 0;
        thread->condvar_wait_address = 0;
        thread->wait_handle = 0;

        auto lock_owner = thread->lock_owner;
        // Threads waking up by timeout from WaitProcessWideKey do not perform priority inheritance
        // and don't have a lock owner unless SignalProcessWideKey was called first and the thread
        // wasn't awakened due to the mutex already being acquired.
        if (lock_owner) {
            lock_owner->RemoveMutexWaiter(thread);
        }
    }

    if (thread->arb_wait_address != 0) {
        ASSERT(thread->status == ThreadStatus::WaitArb);
        thread->arb_wait_address = 0;
    }

    if (resume)
        thread->ResumeFromWait();
}

void Thread::WakeAfterDelay(s64 nanoseconds) {
    // Don't schedule a wakeup if the thread wants to wait forever
    if (nanoseconds == -1)
        return;

    CoreTiming::ScheduleEvent(CoreTiming::nsToCycles(nanoseconds), ThreadWakeupEventType,
                              callback_handle);
}

void Thread::CancelWakeupTimer() {
    CoreTiming::UnscheduleEvent(ThreadWakeupEventType, callback_handle);
}

static boost::optional<s32> GetNextProcessorId(u64 mask) {
    for (s32 index = 0; index < Core::NUM_CPU_CORES; ++index) {
        if (mask & (1ULL << index)) {
            if (!Core::System::GetInstance().Scheduler(index)->GetCurrentThread()) {
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

    boost::optional<s32> new_processor_id = GetNextProcessorId(affinity_mask);
    if (!new_processor_id) {
        new_processor_id = processor_id;
    }
    if (ideal_core != -1 &&
        Core::System::GetInstance().Scheduler(ideal_core)->GetCurrentThread() == nullptr) {
        new_processor_id = ideal_core;
    }

    ASSERT(*new_processor_id < 4);

    // Add thread to new core's scheduler
    auto& next_scheduler = Core::System::GetInstance().Scheduler(*new_processor_id);

    if (*new_processor_id != processor_id) {
        // Remove thread from previous core's scheduler
        scheduler->RemoveThread(this);
        next_scheduler->AddThread(this, current_priority);
    }

    processor_id = *new_processor_id;

    // If the thread was ready, unschedule from the previous core and schedule on the new core
    scheduler->UnscheduleThread(this, current_priority);
    next_scheduler->ScheduleThread(this, current_priority);

    // Change thread's scheduler
    scheduler = next_scheduler;

    Core::System::GetInstance().CpuCore(processor_id).PrepareReschedule();
}

/**
 * Finds a free location for the TLS section of a thread.
 * @param tls_slots The TLS page array of the thread's owner process.
 * Returns a tuple of (page, slot, alloc_needed) where:
 * page: The index of the first allocated TLS page that has free slots.
 * slot: The index of the first free slot in the indicated page.
 * alloc_needed: Whether there's a need to allocate a new TLS page (All pages are full).
 */
static std::tuple<std::size_t, std::size_t, bool> GetFreeThreadLocalSlot(
    const std::vector<std::bitset<8>>& tls_slots) {
    // Iterate over all the allocated pages, and try to find one where not all slots are used.
    for (std::size_t page = 0; page < tls_slots.size(); ++page) {
        const auto& page_tls_slots = tls_slots[page];
        if (!page_tls_slots.all()) {
            // We found a page with at least one free slot, find which slot it is
            for (std::size_t slot = 0; slot < page_tls_slots.size(); ++slot) {
                if (!page_tls_slots.test(slot)) {
                    return std::make_tuple(page, slot, false);
                }
            }
        }
    }

    return std::make_tuple(0, 0, true);
}

/**
 * Resets a thread context, making it ready to be scheduled and run by the CPU
 * @param context Thread context to reset
 * @param stack_top Address of the top of the stack
 * @param entry_point Address of entry point for execution
 * @param arg User argument for thread
 */
static void ResetThreadContext(ARM_Interface::ThreadContext& context, VAddr stack_top,
                               VAddr entry_point, u64 arg) {
    memset(&context, 0, sizeof(ARM_Interface::ThreadContext));

    context.cpu_registers[0] = arg;
    context.pc = entry_point;
    context.sp = stack_top;
    context.cpsr = 0;
    context.fpscr = 0;
}

ResultVal<SharedPtr<Thread>> Thread::Create(std::string name, VAddr entry_point, u32 priority,
                                            u64 arg, s32 processor_id, VAddr stack_top,
                                            SharedPtr<Process> owner_process) {
    // Check if priority is in ranged. Lowest priority -> highest priority id.
    if (priority > THREADPRIO_LOWEST) {
        LOG_ERROR(Kernel_SVC, "Invalid thread priority: {}", priority);
        return ERR_OUT_OF_RANGE;
    }

    if (processor_id > THREADPROCESSORID_MAX) {
        LOG_ERROR(Kernel_SVC, "Invalid processor id: {}", processor_id);
        return ERR_OUT_OF_RANGE_KERNEL;
    }

    // TODO(yuriks): Other checks, returning 0xD9001BEA

    if (!Memory::IsValidVirtualAddress(*owner_process, entry_point)) {
        LOG_ERROR(Kernel_SVC, "(name={}): invalid entry {:016X}", name, entry_point);
        // TODO (bunnei): Find the correct error code to use here
        return ResultCode(-1);
    }

    SharedPtr<Thread> thread(new Thread);

    thread->thread_id = NewThreadId();
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
    thread->callback_handle = wakeup_callback_handle_table.Create(thread).Unwrap();
    thread->owner_process = owner_process;
    thread->scheduler = Core::System::GetInstance().Scheduler(processor_id);
    thread->scheduler->AddThread(thread, priority);

    // Find the next available TLS index, and mark it as used
    auto& tls_slots = owner_process->tls_slots;

    auto [available_page, available_slot, needs_allocation] = GetFreeThreadLocalSlot(tls_slots);
    if (needs_allocation) {
        tls_slots.emplace_back(0); // The page is completely available at the start
        available_page = tls_slots.size() - 1;
        available_slot = 0; // Use the first slot in the new page

        // Allocate some memory from the end of the linear heap for this region.
        const size_t offset = thread->tls_memory->size();
        thread->tls_memory->insert(thread->tls_memory->end(), Memory::PAGE_SIZE, 0);

        auto& vm_manager = owner_process->vm_manager;
        vm_manager.RefreshMemoryBlockMappings(thread->tls_memory.get());

        vm_manager.MapMemoryBlock(Memory::TLS_AREA_VADDR + available_page * Memory::PAGE_SIZE,
                                  thread->tls_memory, 0, Memory::PAGE_SIZE,
                                  MemoryState::ThreadLocal);
    }

    // Mark the slot as used
    tls_slots[available_page].set(available_slot);
    thread->tls_address = Memory::TLS_AREA_VADDR + available_page * Memory::PAGE_SIZE +
                          available_slot * Memory::TLS_ENTRY_SIZE;

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

SharedPtr<Thread> SetupMainThread(VAddr entry_point, u32 priority,
                                  SharedPtr<Process> owner_process) {
    // Setup page table so we can write to memory
    SetCurrentPageTable(&Core::CurrentProcess()->vm_manager.page_table);

    // Initialize new "main" thread
    auto thread_res = Thread::Create("main", entry_point, priority, 0, THREADPROCESSORID_0,
                                     Memory::STACK_AREA_VADDR_END, std::move(owner_process));

    SharedPtr<Thread> thread = std::move(thread_res).Unwrap();

    // Register 1 must be a handle to the main thread
    thread->guest_handle = Kernel::g_handle_table.Create(thread).Unwrap();

    thread->context.cpu_registers[1] = thread->guest_handle;

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

void Thread::AddMutexWaiter(SharedPtr<Thread> thread) {
    thread->lock_owner = this;
    wait_mutex_threads.emplace_back(std::move(thread));
    UpdatePriority();
}

void Thread::RemoveMutexWaiter(SharedPtr<Thread> thread) {
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

    if (status != ThreadStatus::Ready) {
        return;
    }

    boost::optional<s32> new_processor_id{GetNextProcessorId(affinity_mask)};

    if (!new_processor_id) {
        new_processor_id = processor_id;
    }
    if (ideal_core != -1 &&
        Core::System::GetInstance().Scheduler(ideal_core)->GetCurrentThread() == nullptr) {
        new_processor_id = ideal_core;
    }

    ASSERT(*new_processor_id < 4);

    // Add thread to new core's scheduler
    auto& next_scheduler = Core::System::GetInstance().Scheduler(*new_processor_id);

    if (*new_processor_id != processor_id) {
        // Remove thread from previous core's scheduler
        scheduler->RemoveThread(this);
        next_scheduler->AddThread(this, current_priority);
    }

    processor_id = *new_processor_id;

    // If the thread was ready, unschedule from the previous core and schedule on the new core
    scheduler->UnscheduleThread(this, current_priority);
    next_scheduler->ScheduleThread(this, current_priority);

    // Change thread's scheduler
    scheduler = next_scheduler;

    Core::System::GetInstance().CpuCore(processor_id).PrepareReschedule();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Gets the current thread
 */
Thread* GetCurrentThread() {
    return Core::System::GetInstance().CurrentScheduler().GetCurrentThread();
}

void ThreadingInit() {
    ThreadWakeupEventType = CoreTiming::RegisterEvent("ThreadWakeupCallback", ThreadWakeupCallback);
    next_thread_id = 1;
}

void ThreadingShutdown() {
    Kernel::ClearProcessList();
}

} // namespace Kernel
