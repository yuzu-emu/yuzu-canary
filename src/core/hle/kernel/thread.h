// Copyright 2014 Citra Emulator Project / PPSSPP Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "common/common_types.h"
#include "core/arm/arm_interface.h"
#include "core/hle/kernel/object.h"
#include "core/hle/kernel/wait_object.h"
#include "core/hle/result.h"

enum ThreadPriority : u32 {
    THREADPRIO_HIGHEST = 0,       ///< Highest thread priority
    THREADPRIO_USERLAND_MAX = 24, ///< Highest thread priority for userland apps
    THREADPRIO_DEFAULT = 44,      ///< Default thread priority for userland apps
    THREADPRIO_LOWEST = 63,       ///< Lowest thread priority
};

enum ThreadProcessorId : s32 {
    THREADPROCESSORID_DEFAULT = -2, ///< Run thread on default core specified by exheader
    THREADPROCESSORID_0 = 0,        ///< Run thread on core 0
    THREADPROCESSORID_1 = 1,        ///< Run thread on core 1
    THREADPROCESSORID_2 = 2,        ///< Run thread on core 2
    THREADPROCESSORID_3 = 3,        ///< Run thread on core 3
    THREADPROCESSORID_MAX = 4,      ///< Processor ID must be less than this

    /// Allowed CPU mask
    THREADPROCESSORID_DEFAULT_MASK = (1 << THREADPROCESSORID_0) | (1 << THREADPROCESSORID_1) |
                                     (1 << THREADPROCESSORID_2) | (1 << THREADPROCESSORID_3)
};

enum class ThreadStatus {
    Running,      ///< Currently running
    Ready,        ///< Ready to run
    WaitHLEEvent, ///< Waiting for hle event to finish
    WaitSleep,    ///< Waiting due to a SleepThread SVC
    WaitIPC,      ///< Waiting for the reply from an IPC request
    WaitSynchAny, ///< Waiting due to WaitSynch1 or WaitSynchN with wait_all = false
    WaitSynchAll, ///< Waiting due to WaitSynchronizationN with wait_all = true
    WaitMutex,    ///< Waiting due to an ArbitrateLock/WaitProcessWideKey svc
    WaitArb,      ///< Waiting due to a SignalToAddress/WaitForAddress svc
    Dormant,      ///< Created but not yet made ready
    Dead          ///< Run to completion, or forcefully terminated
};

enum class ThreadWakeupReason {
    Signal, // The thread was woken up by WakeupAllWaitingThreads due to an object signal.
    Timeout // The thread was woken up due to a wait timeout.
};

namespace Kernel {

class Process;
class Scheduler;

class Thread final : public WaitObject {
public:
    /**
     * Creates and returns a new thread. The new thread is immediately scheduled
     * @param name The friendly name desired for the thread
     * @param entry_point The address at which the thread should start execution
     * @param priority The thread's priority
     * @param arg User data to pass to the thread
     * @param processor_id The ID(s) of the processors on which the thread is desired to be run
     * @param stack_top The address of the thread's stack top
     * @param owner_process The parent process for the thread
     * @return A shared pointer to the newly created thread
     */
    static ResultVal<SharedPtr<Thread>> Create(std::string name, VAddr entry_point, u32 priority,
                                               u64 arg, s32 processor_id, VAddr stack_top,
                                               SharedPtr<Process> owner_process);

    std::string GetName() const override {
        return name;
    }
    std::string GetTypeName() const override {
        return "Thread";
    }

    static const HandleType HANDLE_TYPE = HandleType::Thread;
    HandleType GetHandleType() const override {
        return HANDLE_TYPE;
    }

    bool ShouldWait(Thread* thread) const override;
    void Acquire(Thread* thread) override;

    /**
     * Gets the thread's current priority
     * @return The current thread's priority
     */
    u32 GetPriority() const {
        return current_priority;
    }

    /**
     * Sets the thread's current priority
     * @param priority The new priority
     */
    void SetPriority(u32 priority);

    /**
     * Temporarily boosts the thread's priority until the next time it is scheduled
     * @param priority The new priority
     */
    void BoostPriority(u32 priority);

    /// Adds a thread to the list of threads that are waiting for a lock held by this thread.
    void AddMutexWaiter(SharedPtr<Thread> thread);

    /// Removes a thread from the list of threads that are waiting for a lock held by this thread.
    void RemoveMutexWaiter(SharedPtr<Thread> thread);

    /// Recalculates the current priority taking into account priority inheritance.
    void UpdatePriority();

    /// Changes the core that the thread is running or scheduled to run on.
    void ChangeCore(u32 core, u64 mask);

    /**
     * Gets the thread's thread ID
     * @return The thread's ID
     */
    u32 GetThreadId() const {
        return thread_id;
    }

    /**
     * Resumes a thread from waiting
     */
    void ResumeFromWait();

    /**
     * Schedules an event to wake up the specified thread after the specified delay
     * @param nanoseconds The time this thread will be allowed to sleep for
     */
    void WakeAfterDelay(s64 nanoseconds);

    /// Cancel any outstanding wakeup events for this thread
    void CancelWakeupTimer();

    /**
     * Sets the result after the thread awakens (from either WaitSynchronization SVC)
     * @param result Value to set to the returned result
     */
    void SetWaitSynchronizationResult(ResultCode result);

    /**
     * Sets the output parameter value after the thread awakens (from WaitSynchronizationN SVC only)
     * @param output Value to set to the output parameter
     */
    void SetWaitSynchronizationOutput(s32 output);

    /**
     * Retrieves the index that this particular object occupies in the list of objects
     * that the thread passed to WaitSynchronizationN, starting the search from the last element.
     * It is used to set the output value of WaitSynchronizationN when the thread is awakened.
     * When a thread wakes up due to an object signal, the kernel will use the index of the last
     * matching object in the wait objects list in case of having multiple instances of the same
     * object in the list.
     * @param object Object to query the index of.
     */
    s32 GetWaitObjectIndex(WaitObject* object) const;

    /**
     * Stops a thread, invalidating it from further use
     */
    void Stop();

    /*
     * Returns the Thread Local Storage address of the current thread
     * @returns VAddr of the thread's TLS
     */
    VAddr GetTLSAddress() const {
        return tls_address;
    }

    /*
     * Returns the value of the TPIDR_EL0 Read/Write system register for this thread.
     * @returns The value of the TPIDR_EL0 register.
     */
    u64 GetTPIDR_EL0() const {
        return tpidr_el0;
    }

    /*
     * Returns the address of the current thread's command buffer, located in the TLS.
     * @returns VAddr of the thread's command buffer.
     */
    VAddr GetCommandBufferAddress() const;

    /**
     * Returns whether this thread is waiting for all the objects in
     * its wait list to become ready, as a result of a WaitSynchronizationN call
     * with wait_all = true.
     */
    bool IsSleepingOnWaitAll() const {
        return status == ThreadStatus::WaitSynchAll;
    }

    ARM_Interface::ThreadContext context;

    u32 thread_id;

    ThreadStatus status;
    VAddr entry_point;
    VAddr stack_top;

    u32 nominal_priority; ///< Nominal thread priority, as set by the emulated application
    u32 current_priority; ///< Current thread priority, can be temporarily changed

    u64 last_running_ticks; ///< CPU tick when thread was last running

    s32 processor_id;

    VAddr tls_address; ///< Virtual address of the Thread Local Storage of the thread
    u64 tpidr_el0;     ///< TPIDR_EL0 read/write system register.

    SharedPtr<Process> owner_process; ///< Process that owns this thread

    /// Objects that the thread is waiting on, in the same order as they were
    // passed to WaitSynchronization1/N.
    std::vector<SharedPtr<WaitObject>> wait_objects;

    /// List of threads that are waiting for a mutex that is held by this thread.
    std::vector<SharedPtr<Thread>> wait_mutex_threads;

    /// Thread that owns the lock that this thread is waiting for.
    SharedPtr<Thread> lock_owner;

    // If waiting on a ConditionVariable, this is the ConditionVariable  address
    VAddr condvar_wait_address;
    VAddr mutex_wait_address; ///< If waiting on a Mutex, this is the mutex address
    Handle wait_handle;       ///< The handle used to wait for the mutex.

    // If waiting for an AddressArbiter, this is the address being waited on.
    VAddr arb_wait_address{0};

    std::string name;

    /// Handle used by guest emulated application to access this thread
    Handle guest_handle;

    /// Handle used as userdata to reference this object when inserting into the CoreTiming queue.
    Handle callback_handle;

    using WakeupCallback = bool(ThreadWakeupReason reason, SharedPtr<Thread> thread,
                                SharedPtr<WaitObject> object, size_t index);
    // Callback that will be invoked when the thread is resumed from a waiting state. If the thread
    // was waiting via WaitSynchronizationN then the object will be the last object that became
    // available. In case of a timeout, the object will be nullptr.
    std::function<WakeupCallback> wakeup_callback;

    std::shared_ptr<Scheduler> scheduler;

    u32 ideal_core{0xFFFFFFFF};
    u64 affinity_mask{0x1};

private:
    Thread();
    ~Thread() override;

    std::shared_ptr<std::vector<u8>> tls_memory = std::make_shared<std::vector<u8>>();
};

/**
 * Sets up the primary application thread
 * @param entry_point The address at which the thread should start execution
 * @param priority The priority to give the main thread
 * @param owner_process The parent process for the main thread
 * @return A shared pointer to the main thread
 */
SharedPtr<Thread> SetupMainThread(VAddr entry_point, u32 priority,
                                  SharedPtr<Process> owner_process);

/**
 * Gets the current thread
 */
Thread* GetCurrentThread();

/**
 * Waits the current thread on a sleep
 */
void WaitCurrentThread_Sleep();

/**
 * Stops the current thread and removes it from the thread_list
 */
void ExitCurrentThread();

/**
 * Initialize threading
 */
void ThreadingInit();

/**
 * Shutdown threading
 */
void ThreadingShutdown();

} // namespace Kernel
