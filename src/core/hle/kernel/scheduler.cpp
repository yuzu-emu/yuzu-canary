// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <utility>

#include "common/assert.h"
#include "common/logging/log.h"
#include "core/arm/arm_interface.h"
#include "core/core.h"
#include "core/core_cpu.h"
#include "core/core_timing.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/scheduler.h"

namespace Kernel {

std::mutex Scheduler::scheduler_mutex;

Scheduler::Scheduler(Core::ARM_Interface& cpu_core) : cpu_core(cpu_core) {}

Scheduler::~Scheduler() {
    for (auto& thread : thread_list) {
        thread->Stop();
    }
}

bool Scheduler::HaveReadyThreads() const {
    std::lock_guard<std::mutex> lock(scheduler_mutex);
    return ready_queue.get_first() != nullptr;
}

Thread* Scheduler::GetCurrentThread() const {
    return current_thread.get();
}

u64 Scheduler::GetLastContextSwitchTicks() const {
    return last_context_switch_time;
}

Thread* Scheduler::PopNextReadyThread() {
    Thread* next = nullptr;
    Thread* thread = GetCurrentThread();

    if (thread && thread->GetStatus() == ThreadStatus::Running) {
        // We have to do better than the current thread.
        // This call returns null when that's not possible.
        next = ready_queue.pop_first_better(thread->GetPriority());
        if (!next) {
            // Otherwise just keep going with the current thread
            next = thread;
        }
    } else {
        next = ready_queue.pop_first();
    }

    return next;
}

void Scheduler::SwitchContext(Thread* new_thread) {
    Thread* const previous_thread = GetCurrentThread();
    Process* const previous_process = Core::CurrentProcess();

    UpdateLastContextSwitchTime(previous_thread, previous_process);

    // Save context for previous thread
    if (previous_thread) {
        cpu_core.SaveContext(previous_thread->GetContext());
        // Save the TPIDR_EL0 system register in case it was modified.
        previous_thread->SetTPIDR_EL0(cpu_core.GetTPIDR_EL0());

        if (previous_thread->GetStatus() == ThreadStatus::Running) {
            // This is only the case when a reschedule is triggered without the current thread
            // yielding execution (i.e. an event triggered, system core time-sliced, etc)
            ready_queue.push_front(previous_thread->GetPriority(), previous_thread);
            previous_thread->SetStatus(ThreadStatus::Ready);
        }
    }

    // Load context of new thread
    if (new_thread) {
        ASSERT_MSG(new_thread->GetStatus() == ThreadStatus::Ready,
                   "Thread must be ready to become running.");

        // Cancel any outstanding wakeup events for this thread
        new_thread->CancelWakeupTimer();

        current_thread = new_thread;

        ready_queue.remove(new_thread->GetPriority(), new_thread);
        new_thread->SetStatus(ThreadStatus::Running);

        auto* const thread_owner_process = current_thread->GetOwnerProcess();
        if (previous_process != thread_owner_process) {
            Core::System::GetInstance().Kernel().MakeCurrentProcess(thread_owner_process);
            SetCurrentPageTable(&Core::CurrentProcess()->VMManager().page_table);
        }

        cpu_core.LoadContext(new_thread->GetContext());
        cpu_core.SetTlsAddress(new_thread->GetTLSAddress());
        cpu_core.SetTPIDR_EL0(new_thread->GetTPIDR_EL0());
        cpu_core.ClearExclusiveState();
    } else {
        current_thread = nullptr;
        // Note: We do not reset the current process and current page table when idling because
        // technically we haven't changed processes, our threads are just paused.
    }
}

void Scheduler::UpdateLastContextSwitchTime(Thread* thread, Process* process) {
    const u64 prev_switch_ticks = last_context_switch_time;
    const u64 most_recent_switch_ticks = CoreTiming::GetTicks();
    const u64 update_ticks = most_recent_switch_ticks - prev_switch_ticks;

    if (thread != nullptr) {
        thread->UpdateCPUTimeTicks(update_ticks);
    }

    if (process != nullptr) {
        process->UpdateCPUTimeTicks(update_ticks);
    }

    last_context_switch_time = most_recent_switch_ticks;
}

void Scheduler::Reschedule() {
    std::lock_guard<std::mutex> lock(scheduler_mutex);

    Thread* cur = GetCurrentThread();
    Thread* next = PopNextReadyThread();

    if (cur && next) {
        LOG_TRACE(Kernel, "context switch {} -> {}", cur->GetObjectId(), next->GetObjectId());
    } else if (cur) {
        LOG_TRACE(Kernel, "context switch {} -> idle", cur->GetObjectId());
    } else if (next) {
        LOG_TRACE(Kernel, "context switch idle -> {}", next->GetObjectId());
    }

    SwitchContext(next);
}

void Scheduler::AddThread(SharedPtr<Thread> thread, u32 priority) {
    std::lock_guard<std::mutex> lock(scheduler_mutex);

    thread_list.push_back(std::move(thread));
    ready_queue.prepare(priority);
}

void Scheduler::RemoveThread(Thread* thread) {
    std::lock_guard<std::mutex> lock(scheduler_mutex);

    thread_list.erase(std::remove(thread_list.begin(), thread_list.end(), thread),
                      thread_list.end());
}

void Scheduler::ScheduleThread(Thread* thread, u32 priority) {
    std::lock_guard<std::mutex> lock(scheduler_mutex);

    ASSERT(thread->GetStatus() == ThreadStatus::Ready);
    ready_queue.push_back(priority, thread);
}

void Scheduler::UnscheduleThread(Thread* thread, u32 priority) {
    std::lock_guard<std::mutex> lock(scheduler_mutex);

    ASSERT(thread->GetStatus() == ThreadStatus::Ready);
    ready_queue.remove(priority, thread);
}

void Scheduler::SetThreadPriority(Thread* thread, u32 priority) {
    std::lock_guard<std::mutex> lock(scheduler_mutex);

    // If thread was ready, adjust queues
    if (thread->GetStatus() == ThreadStatus::Ready)
        ready_queue.move(thread, thread->GetPriority(), priority);
    else
        ready_queue.prepare(priority);
}

Thread* Scheduler::GetNextSuggestedThread(u32 core, u32 maximum_priority) const {
    std::lock_guard<std::mutex> lock(scheduler_mutex);

    const u32 mask = 1U << core;
    return ready_queue.get_first_filter([mask, maximum_priority](Thread const* thread) {
        return (thread->GetAffinityMask() & mask) != 0 && thread->GetPriority() < maximum_priority;
    });
}

void Scheduler::YieldWithoutLoadBalancing(Thread* thread) {
    ASSERT(thread != nullptr);
    // Avoid yielding if the thread isn't even running.
    ASSERT(thread->GetStatus() == ThreadStatus::Running);

    // Sanity check that the priority is valid
    ASSERT(thread->GetPriority() < THREADPRIO_COUNT);

    // Yield this thread -- sleep for zero time and force reschedule to different thread
    WaitCurrentThread_Sleep();
    GetCurrentThread()->WakeAfterDelay(0);
}

void Scheduler::YieldWithLoadBalancing(Thread* thread) {
    ASSERT(thread != nullptr);
    const auto priority = thread->GetPriority();
    const auto core = static_cast<u32>(thread->GetProcessorID());

    // Avoid yielding if the thread isn't even running.
    ASSERT(thread->GetStatus() == ThreadStatus::Running);

    // Sanity check that the priority is valid
    ASSERT(priority < THREADPRIO_COUNT);

    // Sleep for zero time to be able to force reschedule to different thread
    WaitCurrentThread_Sleep();
    GetCurrentThread()->WakeAfterDelay(0);

    Thread* suggested_thread = nullptr;

    // Search through all of the cpu cores (except this one) for a suggested thread.
    // Take the first non-nullptr one
    for (unsigned cur_core = 0; cur_core < Core::NUM_CPU_CORES; ++cur_core) {
        const auto res =
            Core::System::GetInstance().CpuCore(cur_core).Scheduler().GetNextSuggestedThread(
                core, priority);

        // If scheduler provides a suggested thread
        if (res != nullptr) {
            // And its better than the current suggested thread (or is the first valid one)
            if (suggested_thread == nullptr ||
                suggested_thread->GetPriority() > res->GetPriority()) {
                suggested_thread = res;
            }
        }
    }

    // If a suggested thread was found, queue that for this core
    if (suggested_thread != nullptr)
        suggested_thread->ChangeCore(core, suggested_thread->GetAffinityMask());
}

void Scheduler::YieldAndWaitForLoadBalancing(Thread* thread) {
    UNIMPLEMENTED_MSG("Wait for load balancing thread yield type is not implemented!");
}

} // namespace Kernel
