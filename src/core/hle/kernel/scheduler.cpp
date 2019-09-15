// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.
//
// SelectThreads, Yield functions originally by TuxSH.
// licensed under GPLv2 or later under exception provided by the author.

#include <algorithm>
#include <set>
#include <unordered_set>
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

GlobalScheduler::GlobalScheduler(Core::System& system) : system{system} {
    reselection_pending = false;
}

void GlobalScheduler::AddThread(SharedPtr<Thread> thread) {
    thread_list.push_back(std::move(thread));
}

void GlobalScheduler::RemoveThread(const Thread* thread) {
    thread_list.erase(std::remove(thread_list.begin(), thread_list.end(), thread),
                      thread_list.end());
}

/*
 * UnloadThread selects a core and forces it to unload its current thread's context
 */
void GlobalScheduler::UnloadThread(s32 core) {
    Scheduler& sched = system.Scheduler(core);
    sched.UnloadThread();
}

/*
 * SelectThread takes care of selecting the new scheduled thread.
 * It does it in 3 steps:
 * - First a thread is selected from the top of the priority queue. If no thread
 * is obtained then we move to step two, else we are done.
 * - Second we try to get a suggested thread that's not assigned to any core or
 * that is not the top thread in that core.
 * - Third is no suggested thread is found, we do a second pass and pick a running
 * thread in another core and swap it with its current thread.
 */
void GlobalScheduler::SelectThread(u32 core) {
    const auto update_thread = [](Thread* thread, Scheduler& sched) {
        if (thread != sched.selected_thread) {
            if (thread == nullptr) {
                ++sched.idle_selection_count;
            }
            sched.selected_thread = thread;
        }
        sched.context_switch_pending = sched.selected_thread != sched.current_thread;
        std::atomic_thread_fence(std::memory_order_seq_cst);
    };
    Scheduler& sched = system.Scheduler(core);
    Thread* current_thread = nullptr;
    // Step 1: Get top thread in schedule queue.
    current_thread = scheduled_queue[core].empty() ? nullptr : scheduled_queue[core].front();
    if (current_thread) {
        update_thread(current_thread, sched);
        return;
    }
    // Step 2: Try selecting a suggested thread.
    Thread* winner = nullptr;
    std::set<s32> sug_cores;
    for (auto thread : suggested_queue[core]) {
        s32 this_core = thread->GetProcessorID();
        Thread* thread_on_core = nullptr;
        if (this_core >= 0) {
            thread_on_core = scheduled_queue[this_core].front();
        }
        if (this_core < 0 || thread != thread_on_core) {
            winner = thread;
            break;
        }
        sug_cores.insert(this_core);
    }
    // if we got a suggested thread, select it, else do a second pass.
    if (winner && winner->GetPriority() > 2) {
        if (winner->IsRunning()) {
            UnloadThread(winner->GetProcessorID());
        }
        TransferToCore(winner->GetPriority(), core, winner);
        update_thread(winner, sched);
        return;
    }
    // Step 3: Select a suggested thread from another core
    for (auto& src_core : sug_cores) {
        auto it = scheduled_queue[src_core].begin();
        it++;
        if (it != scheduled_queue[src_core].end()) {
            Thread* thread_on_core = scheduled_queue[src_core].front();
            Thread* to_change = *it;
            if (thread_on_core->IsRunning() || to_change->IsRunning()) {
                UnloadThread(src_core);
            }
            TransferToCore(thread_on_core->GetPriority(), core, thread_on_core);
            current_thread = thread_on_core;
            break;
        }
    }
    update_thread(current_thread, sched);
}

/*
 * YieldThread takes a thread and moves it to the back of the it's priority list
 * This operation can be redundant and no scheduling is changed if marked as so.
 */
void GlobalScheduler::YieldThread(Thread* yielding_thread) {
    // Note: caller should use critical section, etc.
    const u32 core_id = static_cast<u32>(yielding_thread->GetProcessorID());
    const u32 priority = yielding_thread->GetPriority();

    // Yield the thread
    ASSERT_MSG(yielding_thread == scheduled_queue[core_id].front(priority),
               "Thread yielding without being in front");
    scheduled_queue[core_id].yield(priority);

    Thread* winner = scheduled_queue[core_id].front(priority);
    AskForReselectionOrMarkRedundant(yielding_thread, winner);
}

/*
 * YieldThreadAndBalanceLoad takes a thread and moves it to the back of the it's priority list.
 * Afterwards, tries to pick a suggested thread from the suggested queue that has worse time or
 * a better priority than the next thread in the core.
 * This operation can be redundant and no scheduling is changed if marked as so.
 */
void GlobalScheduler::YieldThreadAndBalanceLoad(Thread* yielding_thread) {
    // Note: caller should check if !thread.IsSchedulerOperationRedundant and use critical section,
    // etc.
    const u32 core_id = static_cast<u32>(yielding_thread->GetProcessorID());
    const u32 priority = yielding_thread->GetPriority();

    // Yield the thread
    ASSERT_MSG(yielding_thread == scheduled_queue[core_id].front(priority),
               "Thread yielding without being in front");
    scheduled_queue[core_id].yield(priority);

    std::array<Thread*, NUM_CPU_CORES> current_threads;
    for (u32 i = 0; i < NUM_CPU_CORES; i++) {
        current_threads[i] = scheduled_queue[i].empty() ? nullptr : scheduled_queue[i].front();
    }

    Thread* next_thread = scheduled_queue[core_id].front(priority);
    Thread* winner = nullptr;
    for (auto& thread : suggested_queue[core_id]) {
        const s32 source_core = thread->GetProcessorID();
        if (source_core >= 0) {
            if (current_threads[source_core] != nullptr) {
                if (thread == current_threads[source_core] ||
                    current_threads[source_core]->GetPriority() < min_regular_priority) {
                    continue;
                }
            }
            if (next_thread->GetLastRunningTicks() >= thread->GetLastRunningTicks() ||
                next_thread->GetPriority() < thread->GetPriority()) {
                if (thread->GetPriority() <= priority) {
                    winner = thread;
                    break;
                }
            }
        }
    }

    if (winner != nullptr) {
        if (winner != yielding_thread) {
            if (winner->IsRunning()) {
                UnloadThread(winner->GetProcessorID());
            }
            TransferToCore(winner->GetPriority(), core_id, winner);
        }
    } else {
        winner = next_thread;
    }

    AskForReselectionOrMarkRedundant(yielding_thread, winner);
}

/*
 * YieldThreadAndWaitForLoadBalancing takes a thread and moves it out of the scheduling queue
 * and into the suggested queue. If no thread can be squeduled afterwards in that core,
 * a suggested thread is obtained instead.
 * This operation can be redundant and no scheduling is changed if marked as so.
 */
void GlobalScheduler::YieldThreadAndWaitForLoadBalancing(Thread* yielding_thread) {
    // Note: caller should check if !thread.IsSchedulerOperationRedundant and use critical section,
    // etc.
    Thread* winner = nullptr;
    const u32 core_id = static_cast<u32>(yielding_thread->GetProcessorID());

    // Remove the thread from its scheduled mlq, put it on the corresponding "suggested" one instead
    TransferToCore(yielding_thread->GetPriority(), -1, yielding_thread);

    // If the core is idle, perform load balancing, excluding the threads that have just used this
    // function...
    if (scheduled_queue[core_id].empty()) {
        // Here, "current_threads" is calculated after the ""yield"", unlike yield -1
        std::array<Thread*, NUM_CPU_CORES> current_threads;
        for (u32 i = 0; i < NUM_CPU_CORES; i++) {
            current_threads[i] = scheduled_queue[i].empty() ? nullptr : scheduled_queue[i].front();
        }
        for (auto& thread : suggested_queue[core_id]) {
            const s32 source_core = thread->GetProcessorID();
            if (source_core < 0 || thread == current_threads[source_core]) {
                continue;
            }
            if (current_threads[source_core] == nullptr ||
                current_threads[source_core]->GetPriority() >= min_regular_priority) {
                winner = thread;
            }
            break;
        }
        if (winner != nullptr) {
            if (winner != yielding_thread) {
                if (winner->IsRunning()) {
                    UnloadThread(winner->GetProcessorID());
                }
                TransferToCore(winner->GetPriority(), core_id, winner);
            }
        } else {
            winner = yielding_thread;
        }
    }

    AskForReselectionOrMarkRedundant(yielding_thread, winner);
}

void GlobalScheduler::Schedule(u32 priority, u32 core, Thread* thread) {
    ASSERT_MSG(thread->GetProcessorID() == core, "Thread must be assigned to this core.");
    scheduled_queue[core].add(thread, priority);
}

void GlobalScheduler::SchedulePrepend(u32 priority, u32 core, Thread* thread) {
    ASSERT_MSG(thread->GetProcessorID() == core, "Thread must be assigned to this core.");
    scheduled_queue[core].add(thread, priority, false);
}

void GlobalScheduler::AskForReselectionOrMarkRedundant(Thread* current_thread, Thread* winner) {
    if (current_thread == winner) {
        // TODO(blinkhawk): manage redundant operations, this is not implemented.
        // as its mostly an optimization.
        // current_thread->SetRedundantSchedulerOperation();
    } else {
        reselection_pending.store(true, std::memory_order_release);
    }
}

GlobalScheduler::~GlobalScheduler() = default;

Scheduler::Scheduler(Core::System& system, Core::ARM_Interface& cpu_core, u32 core_id)
    : system(system), cpu_core(cpu_core), core_id(core_id) {}

Scheduler::~Scheduler() = default;

bool Scheduler::HaveReadyThreads() const {
    return system.GlobalScheduler().HaveReadyThreads(core_id);
}

Thread* Scheduler::GetCurrentThread() const {
    return current_thread.get();
}

Thread* Scheduler::GetSelectedThread() const {
    return selected_thread.get();
}

void Scheduler::SelectThreads() {
    system.GlobalScheduler().SelectThread(core_id);
}

u64 Scheduler::GetLastContextSwitchTicks() const {
    return last_context_switch_time;
}

void Scheduler::TryDoContextSwitch() {
    if (context_switch_pending) {
        SwitchContext();
    }
}

void Scheduler::UnloadThread() {
    Thread* const previous_thread = GetCurrentThread();
    Process* const previous_process = system.Kernel().CurrentProcess();

    UpdateLastContextSwitchTime(previous_thread, previous_process);

    // Save context for previous thread
    if (previous_thread) {
        cpu_core.SaveContext(previous_thread->GetContext());
        // Save the TPIDR_EL0 system register in case it was modified.
        previous_thread->SetTPIDR_EL0(cpu_core.GetTPIDR_EL0());

        if (previous_thread->GetStatus() == ThreadStatus::Running) {
            // This is only the case when a reschedule is triggered without the current thread
            // yielding execution (i.e. an event triggered, system core time-sliced, etc)
            previous_thread->SetStatus(ThreadStatus::Ready);
        }
        previous_thread->SetIsRunning(false);
    }
    current_thread = nullptr;
}

void Scheduler::SwitchContext() {
    Thread* const previous_thread = GetCurrentThread();
    Thread* const new_thread = GetSelectedThread();

    context_switch_pending = false;
    if (new_thread == previous_thread) {
        return;
    }

    Process* const previous_process = system.Kernel().CurrentProcess();

    UpdateLastContextSwitchTime(previous_thread, previous_process);

    // Save context for previous thread
    if (previous_thread) {
        cpu_core.SaveContext(previous_thread->GetContext());
        // Save the TPIDR_EL0 system register in case it was modified.
        previous_thread->SetTPIDR_EL0(cpu_core.GetTPIDR_EL0());

        if (previous_thread->GetStatus() == ThreadStatus::Running) {
            // This is only the case when a reschedule is triggered without the current thread
            // yielding execution (i.e. an event triggered, system core time-sliced, etc)
            previous_thread->SetStatus(ThreadStatus::Ready);
        }
        previous_thread->SetIsRunning(false);
    }

    // Load context of new thread
    if (new_thread) {
        ASSERT_MSG(new_thread->GetProcessorID() == this->core_id,
                   "Thread must be assigned to this core.");
        ASSERT_MSG(new_thread->GetStatus() == ThreadStatus::Ready,
                   "Thread must be ready to become running.");

        // Cancel any outstanding wakeup events for this thread
        new_thread->CancelWakeupTimer();
        current_thread = new_thread;
        new_thread->SetStatus(ThreadStatus::Running);
        new_thread->SetIsRunning(true);

        auto* const thread_owner_process = current_thread->GetOwnerProcess();
        if (previous_process != thread_owner_process) {
            system.Kernel().MakeCurrentProcess(thread_owner_process);
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
    const u64 most_recent_switch_ticks = system.CoreTiming().GetTicks();
    const u64 update_ticks = most_recent_switch_ticks - prev_switch_ticks;

    if (thread != nullptr) {
        thread->UpdateCPUTimeTicks(update_ticks);
    }

    if (process != nullptr) {
        process->UpdateCPUTimeTicks(update_ticks);
    }

    last_context_switch_time = most_recent_switch_ticks;
}

} // namespace Kernel
