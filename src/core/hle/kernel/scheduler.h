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

    /// Sets the priority of a thread in the scheduler
    void SetThreadPriority(Thread* thread, u32 priority);

    /// Gets the next suggested thread for load balancing
    Thread* GetNextSuggestedThread(u32 core, u32 minimum_priority) const;

    /**
     * YieldWithoutLoadBalancing -- analogous to normal yield on a system
     * Moves the thread to the end of the ready queue for its priority, and then reschedules the
     * system to the new head of the queue.
     *
     * Example (Single Core -- but can be extrapolated to multi):
     * ready_queue[prio=0]: ThreadA, ThreadB, ThreadC (->exec order->)
     * Currently Running: ThreadR
     *
     * ThreadR calls YieldWithoutLoadBalancing
     *
     * ThreadR is moved to the end of ready_queue[prio=0]:
     * ready_queue[prio=0]: ThreadA, ThreadB, ThreadC, ThreadR (->exec order->)
     * Currently Running: Nothing
     *
     * System is rescheduled (ThreadA is popped off of queue):
     * ready_queue[prio=0]: ThreadB, ThreadC, ThreadR (->exec order->)
     * Currently Running: ThreadA
     *
     * If the queue is empty at time of call, no yielding occurs. This does not cross between cores
     * or priorities at all.
     */
    void YieldWithoutLoadBalancing(Thread* thread);

    /**
     * YieldWithLoadBalancing -- yield but with better selection of the new running thread
     * Moves the current thread to the end of the ready queue for its priority, then selects a
     * 'suggested thread' (a thread on a different core that could run on this core) from the
     * scheduler, changes its core, and reschedules the current core to that thread.
     *
     * Example (Dual Core -- can be extrapolated to Quad Core, this is just normal yield if it were
     * single core):
     * ready_queue[core=0][prio=0]: ThreadA, ThreadB (affinities not pictured as irrelevant
     * ready_queue[core=1][prio=0]: ThreadC[affinity=both], ThreadD[affinity=core1only]
     * Currently Running: ThreadQ on Core 0 || ThreadP on Core 1
     *
     * ThreadQ calls YieldWithLoadBalancing
     *
     * ThreadQ is moved to the end of ready_queue[core=0][prio=0]:
     * ready_queue[core=0][prio=0]: ThreadA, ThreadB
     * ready_queue[core=1][prio=0]: ThreadC[affinity=both], ThreadD[affinity=core1only]
     * Currently Running: ThreadQ on Core 0 || ThreadP on Core 1
     *
     * A list of suggested threads for each core is compiled
     * Suggested Threads: {ThreadC on Core 1}
     * If this were quad core (as the switch is), there could be between 0 and 3 threads in this
     * list. If there are more than one, the thread is selected by highest prio.
     *
     * ThreadC is core changed to Core 0:
     * ready_queue[core=0][prio=0]: ThreadC, ThreadA, ThreadB, ThreadQ
     * ready_queue[core=1][prio=0]: ThreadD
     * Currently Running: None on Core 0 || ThreadP on Core 1
     *
     * System is rescheduled (ThreadC is popped off of queue):
     * ready_queue[core=0][prio=0]: ThreadA, ThreadB, ThreadQ
     * ready_queue[core=1][prio=0]: ThreadD
     * Currently Running: ThreadC on Core 0 || ThreadP on Core 1
     *
     * If no suggested threads can be found this will behave just as normal yield. If there are
     * multiple candidates for the suggested thread on a core, the highest prio is taken.
     */
    void YieldWithLoadBalancing(Thread* thread);

    /// Currently unknown -- asserts as unimplemented on call
    void YieldAndWaitForLoadBalancing(Thread* thread);

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
