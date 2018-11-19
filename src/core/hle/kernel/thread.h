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

namespace Kernel {

class KernelCore;
class Process;
class Scheduler;

enum ThreadPriority : u32 {
    THREADPRIO_HIGHEST = 0,       ///< Highest thread priority
    THREADPRIO_USERLAND_MAX = 24, ///< Highest thread priority for userland apps
    THREADPRIO_DEFAULT = 44,      ///< Default thread priority for userland apps
    THREADPRIO_LOWEST = 63,       ///< Lowest thread priority
    THREADPRIO_COUNT = 64,        ///< Total number of possible thread priorities.
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

class Thread final : public WaitObject {
public:
    using TLSMemory = std::vector<u8>;
    using TLSMemoryPtr = std::shared_ptr<TLSMemory>;

    using MutexWaitingThreads = std::vector<SharedPtr<Thread>>;

    using ThreadContext = Core::ARM_Interface::ThreadContext;

    using ThreadWaitObjects = std::vector<SharedPtr<WaitObject>>;

    using WakeupCallback = std::function<bool(ThreadWakeupReason reason, SharedPtr<Thread> thread,
                                              SharedPtr<WaitObject> object, std::size_t index)>;

    /**
     * Creates and returns a new thread. The new thread is immediately scheduled
     * @param kernel The kernel instance this thread will be created under.
     * @param name The friendly name desired for the thread
     * @param entry_point The address at which the thread should start execution
     * @param priority The thread's priority
     * @param arg User data to pass to the thread
     * @param processor_id The ID(s) of the processors on which the thread is desired to be run
     * @param stack_top The address of the thread's stack top
     * @param owner_process The parent process for the thread
     * @return A shared pointer to the newly created thread
     */
    static ResultVal<SharedPtr<Thread>> Create(KernelCore& kernel, std::string name,
                                               VAddr entry_point, u32 priority, u64 arg,
                                               s32 processor_id, VAddr stack_top,
                                               Process& owner_process);

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
     * Gets the thread's nominal priority.
     * @return The current thread's nominal priority.
     */
    u32 GetNominalPriority() const {
        return nominal_priority;
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
    u32 GetThreadID() const {
        return thread_id;
    }

    TLSMemoryPtr& GetTLSMemory() {
        return tls_memory;
    }

    const TLSMemoryPtr& GetTLSMemory() const {
        return tls_memory;
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

    /// Sets the value of the TPIDR_EL0 Read/Write system register for this thread.
    void SetTPIDR_EL0(u64 value) {
        tpidr_el0 = value;
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

    ThreadContext& GetContext() {
        return context;
    }

    const ThreadContext& GetContext() const {
        return context;
    }

    ThreadStatus GetStatus() const {
        return status;
    }

    void SetStatus(ThreadStatus new_status);

    u64 GetLastRunningTicks() const {
        return last_running_ticks;
    }

    u64 GetTotalCPUTimeTicks() const {
        return total_cpu_time_ticks;
    }

    void UpdateCPUTimeTicks(u64 ticks) {
        total_cpu_time_ticks += ticks;
    }

    s32 GetProcessorID() const {
        return processor_id;
    }

    Process* GetOwnerProcess() {
        return owner_process;
    }

    const Process* GetOwnerProcess() const {
        return owner_process;
    }

    const ThreadWaitObjects& GetWaitObjects() const {
        return wait_objects;
    }

    void SetWaitObjects(ThreadWaitObjects objects) {
        wait_objects = std::move(objects);
    }

    void ClearWaitObjects() {
        wait_objects.clear();
    }

    /// Determines whether all the objects this thread is waiting on are ready.
    bool AllWaitObjectsReady();

    const MutexWaitingThreads& GetMutexWaitingThreads() const {
        return wait_mutex_threads;
    }

    Thread* GetLockOwner() const {
        return lock_owner.get();
    }

    void SetLockOwner(SharedPtr<Thread> owner) {
        lock_owner = std::move(owner);
    }

    VAddr GetCondVarWaitAddress() const {
        return condvar_wait_address;
    }

    void SetCondVarWaitAddress(VAddr address) {
        condvar_wait_address = address;
    }

    VAddr GetMutexWaitAddress() const {
        return mutex_wait_address;
    }

    void SetMutexWaitAddress(VAddr address) {
        mutex_wait_address = address;
    }

    Handle GetWaitHandle() const {
        return wait_handle;
    }

    void SetWaitHandle(Handle handle) {
        wait_handle = handle;
    }

    VAddr GetArbiterWaitAddress() const {
        return arb_wait_address;
    }

    void SetArbiterWaitAddress(VAddr address) {
        arb_wait_address = address;
    }

    void SetGuestHandle(Handle handle) {
        guest_handle = handle;
    }

    bool HasWakeupCallback() const {
        return wakeup_callback != nullptr;
    }

    void SetWakeupCallback(WakeupCallback callback) {
        wakeup_callback = std::move(callback);
    }

    void InvalidateWakeupCallback() {
        SetWakeupCallback(nullptr);
    }

    /**
     * Invokes the thread's wakeup callback.
     *
     * @pre A valid wakeup callback has been set. Violating this precondition
     *      will cause an assertion to trigger.
     */
    bool InvokeWakeupCallback(ThreadWakeupReason reason, SharedPtr<Thread> thread,
                              SharedPtr<WaitObject> object, std::size_t index);

    u32 GetIdealCore() const {
        return ideal_core;
    }

    u64 GetAffinityMask() const {
        return affinity_mask;
    }

    void YieldNormal();
    void YieldWithLoadBalancing();
    void YieldAndWaitForLoadBalancing();

private:
    explicit Thread(KernelCore& kernel);
    ~Thread() override;

    void ChangeScheduler();

    Core::ARM_Interface::ThreadContext context{};

    u32 thread_id = 0;

    ThreadStatus status = ThreadStatus::Dormant;

    VAddr entry_point = 0;
    VAddr stack_top = 0;

    u32 nominal_priority = 0; ///< Nominal thread priority, as set by the emulated application
    u32 current_priority = 0; ///< Current thread priority, can be temporarily changed

    u64 total_cpu_time_ticks = 0; ///< Total CPU running ticks.
    u64 last_running_ticks = 0;   ///< CPU tick when thread was last running

    s32 processor_id = 0;

    VAddr tls_address = 0; ///< Virtual address of the Thread Local Storage of the thread
    u64 tpidr_el0 = 0;     ///< TPIDR_EL0 read/write system register.

    /// Process that owns this thread
    Process* owner_process;

    /// Objects that the thread is waiting on, in the same order as they were
    /// passed to WaitSynchronization1/N.
    ThreadWaitObjects wait_objects;

    /// List of threads that are waiting for a mutex that is held by this thread.
    MutexWaitingThreads wait_mutex_threads;

    /// Thread that owns the lock that this thread is waiting for.
    SharedPtr<Thread> lock_owner;

    /// If waiting on a ConditionVariable, this is the ConditionVariable address
    VAddr condvar_wait_address = 0;
    /// If waiting on a Mutex, this is the mutex address
    VAddr mutex_wait_address = 0;
    /// The handle used to wait for the mutex.
    Handle wait_handle = 0;

    /// If waiting for an AddressArbiter, this is the address being waited on.
    VAddr arb_wait_address{0};

    /// Handle used by guest emulated application to access this thread
    Handle guest_handle = 0;

    /// Handle used as userdata to reference this object when inserting into the CoreTiming queue.
    Handle callback_handle = 0;

    /// Callback that will be invoked when the thread is resumed from a waiting state. If the thread
    /// was waiting via WaitSynchronizationN then the object will be the last object that became
    /// available. In case of a timeout, the object will be nullptr.
    WakeupCallback wakeup_callback;

    Scheduler* scheduler = nullptr;

    u32 ideal_core{0xFFFFFFFF};
    u64 affinity_mask{0x1};

    TLSMemoryPtr tls_memory = std::make_shared<TLSMemory>();

    std::string name;
};

/**
 * Sets up the primary application thread
 * @param kernel The kernel instance to create the main thread under.
 * @param entry_point The address at which the thread should start execution
 * @param priority The priority to give the main thread
 * @param owner_process The parent process for the main thread
 * @return A shared pointer to the main thread
 */
SharedPtr<Thread> SetupMainThread(KernelCore& kernel, VAddr entry_point, u32 priority,
                                  Process& owner_process);

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

} // namespace Kernel
