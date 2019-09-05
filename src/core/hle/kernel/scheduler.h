// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <mutex>
#include <vector>
#include "common/common_types.h"
#include "common/multi_level_queue.h"
#include "core/hle/kernel/object.h"
#include "core/hle/kernel/thread.h"

namespace Core {
class ARM_Interface;
class System;
} // namespace Core

namespace Kernel {

class Process;

class GlobalScheduler final {
public:
    static constexpr u32 NUM_CPU_CORES = 4;

    explicit GlobalScheduler(Core::System& system);
    ~GlobalScheduler();
    /// Adds a new thread to the scheduler
    void AddThread(SharedPtr<Thread> thread);

    /// Removes a thread from the scheduler
    void RemoveThread(const Thread* thread);

    /// Returns a list of all threads managed by the scheduler
    const std::vector<SharedPtr<Thread>>& GetThreadList() const {
        return thread_list;
    }

    // Add a thread to the suggested queue of a cpu core. Suggested threads may be
    // picked if no thread is scheduled to run on the core.
    void Suggest(u32 priority, u32 core, Thread* thread) {
        suggested_queue[core].add(thread, priority);
    }

    // Remove a thread to the suggested queue of a cpu core. Suggested threads may be
    // picked if no thread is scheduled to run on the core.
    void Unsuggest(u32 priority, u32 core, Thread* thread) {
        suggested_queue[core].remove(thread, priority);
    }

    // Add a thread to the scheduling queue of a cpu core. The thread is added at the
    // back the queue in its priority level
    void Schedule(u32 priority, u32 core, Thread* thread);

    // Add a thread to the scheduling queue of a cpu core. The thread is added at the
    // front the queue in its priority level
    void SchedulePrepend(u32 priority, u32 core, Thread* thread);

    // Reschedule an already scheduled thread based on a new priority
    void Reschedule(u32 priority, u32 core, Thread* thread) {
        scheduled_queue[core].remove(thread, priority);
        scheduled_queue[core].add(thread, priority);
    }

    // Unschedule a thread.
    void Unschedule(u32 priority, u32 core, Thread* thread) {
        scheduled_queue[core].remove(thread, priority);
    }

    // Transfers a thread into an specific core. If the destination_core is -1
    // it will be unscheduled from its source code and added into its suggested
    // queue.
    void TransferToCore(u32 priority, s32 destination_core, Thread* thread) {
        const bool schedulable = thread->GetPriority() < THREADPRIO_COUNT;
        const s32 source_core = thread->GetProcessorID();
        if (source_core == destination_core || !schedulable) {
            return;
        }
        thread->SetProcessorID(destination_core);
        if (source_core >= 0) {
            Unschedule(priority, source_core, thread);
        }
        if (destination_core >= 0) {
            Unsuggest(priority, destination_core, thread);
            Schedule(priority, destination_core, thread);
        }
        if (source_core >= 0) {
            Suggest(priority, source_core, thread);
        }
    }

    /*
     * UnloadThread selects a core and forces it to unload its current thread's context
     */
    void UnloadThread(s32 core);

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
    void SelectThread(u32 core);

    bool HaveReadyThreads(u32 core_id) const {
        return !scheduled_queue[core_id].empty();
    }

    /*
     * YieldThread takes a thread and moves it to the back of the it's priority list
     * This operation can be redundant and no scheduling is changed if marked as so.
     */
    void YieldThread(Thread* thread);

    /*
     * YieldThreadAndBalanceLoad takes a thread and moves it to the back of the it's priority list.
     * Afterwards, tries to pick a suggested thread from the suggested queue that has worse time or
     * a better priority than the next thread in the core.
     * This operation can be redundant and no scheduling is changed if marked as so.
     */
    void YieldThreadAndBalanceLoad(Thread* thread);

    /*
     * YieldThreadAndWaitForLoadBalancing takes a thread and moves it out of the scheduling queue
     * and into the suggested queue. If no thread can be squeduled afterwards in that core,
     * a suggested thread is obtained instead.
     * This operation can be redundant and no scheduling is changed if marked as so.
     */
    void YieldThreadAndWaitForLoadBalancing(Thread* thread);

    u32 CpuCoresCount() const {
        return NUM_CPU_CORES;
    }

    void SetReselectionPending() {
        reselection_pending.store(true, std::memory_order_release);
    }

    bool IsReselectionPending() const {
        return reselection_pending.load();
    }

private:
    void AskForReselectionOrMarkRedundant(Thread* current_thread, Thread* winner);

    static constexpr u32 min_regular_priority = 2;
    std::array<Common::MultiLevelQueue<Thread*, THREADPRIO_COUNT>, NUM_CPU_CORES> scheduled_queue;
    std::array<Common::MultiLevelQueue<Thread*, THREADPRIO_COUNT>, NUM_CPU_CORES> suggested_queue;
    std::atomic<bool> reselection_pending;

    /// Lists all thread ids that aren't deleted/etc.
    std::vector<SharedPtr<Thread>> thread_list;
    Core::System& system;
};

class Scheduler final {
public:
    explicit Scheduler(Core::System& system, Core::ARM_Interface& cpu_core, const u32 core_id);
    ~Scheduler();

    /// Returns whether there are any threads that are ready to run.
    bool HaveReadyThreads() const;

    /// Reschedules to the next available thread (call after current thread is suspended)
    void TryDoContextSwitch();

    void UnloadThread();

    void SelectThreads();

    /// Gets the current running thread
    Thread* GetCurrentThread() const;

    Thread* GetSelectedThread() const;

    /// Gets the timestamp for the last context switch in ticks.
    u64 GetLastContextSwitchTicks() const;

    bool ContextSwitchPending() const {
        return context_switch_pending;
    }

private:
    friend class GlobalScheduler;
    /**
     * Switches the CPU's active thread context to that of the specified thread
     * @param new_thread The thread to switch to
     */
    void SwitchContext();

    /**
     * Called on every context switch to update the internal timestamp
     * This also updates the running time ticks for the given thread and
     * process using the following difference:
     *
     * ticks += most_recent_ticks - last_context_switch_ticks
     *
     * The internal tick timestamp for the scheduler is simply the
     * most recent tick count retrieved. No special arithmetic is
     * applied to it.
     */
    void UpdateLastContextSwitchTime(Thread* thread, Process* process);

    SharedPtr<Thread> current_thread = nullptr;
    SharedPtr<Thread> selected_thread = nullptr;

    Core::System& system;
    Core::ARM_Interface& cpu_core;
    u64 last_context_switch_time = 0;
    u64 idle_selection_count = 0;
    const u32 core_id;

    bool context_switch_pending = false;
};

} // namespace Kernel
