// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <mutex>
#include <vector>
#include "common/common_types.h"
#include "common/thread_queue_list.h"
#include "core/hle/kernel/object.h"
#include "core/hle/kernel/thread.h"

namespace Core {
class ARM_Interface;
}

namespace Kernel {

class Process;

class Scheduler final {
public:
    explicit Scheduler(Core::ARM_Interface& cpu_core);
    ~Scheduler();

    /// Returns whether there are any threads that are ready to run.
    bool HaveReadyThreads() const;

    /// Reschedules to the next available thread (call after current thread is suspended)
    void Reschedule();

    /// Gets the current running thread
    Thread* GetCurrentThread() const;

    /// Gets the timestamp for the last context switch in ticks.
    u64 GetLastContextSwitchTicks() const;

    /// Adds a new thread to the scheduler
    void AddThread(SharedPtr<Thread> thread, u32 priority);

    /// Removes a thread from the scheduler
    void RemoveThread(Thread* thread);

    /// Schedules a thread that has become "ready"
    void ScheduleThread(Thread* thread, u32 priority);

    /// Unschedules a thread that was already scheduled
    void UnscheduleThread(Thread* thread, u32 priority);

    /// Moves a thread to the back of the current priority queue
    void RescheduleThread(Thread* thread, u32 priority);

    /// Sets the priority of a thread in the scheduler
    void SetThreadPriority(Thread* thread, u32 priority);

    /// Gets the next suggested thread for load balancing
    Thread* GetNextSuggestedThread(u32 core);

    /// Returns a list of all threads managed by the scheduler
    const std::vector<SharedPtr<Thread>>& GetThreadList() const {
        return thread_list;
    }

private:
    /**
     * Pops and returns the next thread from the thread queue
     * @return A pointer to the next ready thread
     */
    Thread* PopNextReadyThread();

    /**
     * Switches the CPU's active thread context to that of the specified thread
     * @param new_thread The thread to switch to
     */
    void SwitchContext(Thread* new_thread);

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

    /// Lists all thread ids that aren't deleted/etc.
    std::vector<SharedPtr<Thread>> thread_list;

    /// Lists only ready thread ids.
    Common::ThreadQueueList<Thread*, THREADPRIO_LOWEST + 1> ready_queue;

    SharedPtr<Thread> current_thread = nullptr;

    Core::ARM_Interface& cpu_core;
    u64 last_context_switch_time = 0;

    static std::mutex scheduler_mutex;
};

} // namespace Kernel
