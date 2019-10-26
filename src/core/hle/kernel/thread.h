// Copyright 2014 Citra Emulator Project / PPSSPP Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
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
    /// Indicates that no particular processor core is preferred.
    THREADPROCESSORID_DONT_CARE = -1,

    /// Run thread on the ideal core specified by the process.
    THREADPROCESSORID_IDEAL = -2,

    /// Indicates that the preferred processor ID shouldn't be updated in
    /// a core mask setting operation.
    THREADPROCESSORID_DONT_UPDATE = -3,

    THREADPROCESSORID_0 = 0,   ///< Run thread on core 0
    THREADPROCESSORID_1 = 1,   ///< Run thread on core 1
    THREADPROCESSORID_2 = 2,   ///< Run thread on core 2
    THREADPROCESSORID_3 = 3,   ///< Run thread on core 3
    THREADPROCESSORID_MAX = 4, ///< Processor ID must be less than this

    /// Allowed CPU mask
    THREADPROCESSORID_DEFAULT_MASK = (1 << THREADPROCESSORID_0) | (1 << THREADPROCESSORID_1) |
                                     (1 << THREADPROCESSORID_2) | (1 << THREADPROCESSORID_3)
};

enum class ThreadStatus {
    Running,      ///< Currently running
    Ready,        ///< Ready to run
    Paused,       ///< Paused by SetThreadActivity or debug
    WaitHLEEvent, ///< Waiting for hle event to finish
    WaitSleep,    ///< Waiting due to a SleepThread SVC
    WaitIPC,      ///< Waiting for the reply from an IPC request
    WaitSynch,    ///< Waiting due to WaitSynchronization
    WaitMutex,    ///< Waiting due to an ArbitrateLock svc
    WaitCondVar,  ///< Waiting due to an WaitProcessWideKey svc
    WaitArb,      ///< Waiting due to a SignalToAddress/WaitForAddress svc
    Dormant,      ///< Created but not yet made ready
    Dead          ///< Run to completion, or forcefully terminated
};

enum class ThreadWakeupReason {
    Signal, // The thread was woken up by WakeupAllWaitingThreads due to an object signal.
    Timeout // The thread was woken up due to a wait timeout.
};

enum class ThreadActivity : u32 {
    Normal = 0,
    Paused = 1,
};

enum class ThreadSchedStatus : u32 {
    None = 0,
    Paused = 1,
    Runnable = 2,
    Exited = 3,
};

enum class ThreadSchedFlags : u32 {
    ProcessPauseFlag = 1 << 4,
    ThreadPauseFlag = 1 << 5,
    ProcessDebugPauseFlag = 1 << 6,
    KernelInitPauseFlag = 1 << 8,
};

enum class ThreadSchedMasks : u32 {
    LowMask = 0x000f,
    HighMask = 0xfff0,
    ForcePauseMask = 0x0070,
};

class Thread final : public WaitObject {
public:
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

    void SetName(std::string new_name) {
        name = std::move(new_name);
    }

    std::string GetTypeName() const override {
        return "Thread";
    }

    static constexpr HandleType HANDLE_TYPE = HandleType::Thread;
    HandleType GetHandleType() const override {
        return HANDLE_TYPE;
    }

    bool ShouldWait(const Thread* thread) const override;
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
    u64 GetThreadID() const {
        return thread_id;
    }

    /// Resumes a thread from waiting
    void ResumeFromWait();

    /// Cancels a waiting operation that this thread may or may not be within.
    ///
    /// When the thread is within a waiting state, this will set the thread's
    /// waiting result to signal a canceled wait. The function will then resume
    /// this thread.
    ///
    void CancelWait();

    /**
     * Schedules an event to wake up the specified thread after the specified delay
     * @param nanoseconds The time this thread will be allowed to sleep for
     */
    void WakeAfterDelay(s64 nanoseconds);

    /// Cancel any outstanding wakeup events for this thread
    void CancelWakeupTimer();

    /**
     * Sets the result after the thread awakens (from svcWaitSynchronization)
     * @param result Value to set to the returned result
     */
    void SetWaitSynchronizationResult(ResultCode result);

    /**
     * Sets the output parameter value after the thread awakens (from svcWaitSynchronization)
     * @param output Value to set to the output parameter
     */
    void SetWaitSynchronizationOutput(s32 output);

    /**
     * Retrieves the index that this particular object occupies in the list of objects
     * that the thread passed to WaitSynchronization, starting the search from the last element.
     *
     * It is used to set the output index of WaitSynchronization when the thread is awakened.
     *
     * When a thread wakes up due to an object signal, the kernel will use the index of the last
     * matching object in the wait objects list in case of having multiple instances of the same
     * object in the list.
     *
     * @param object Object to query the index of.
     */
    s32 GetWaitObjectIndex(const WaitObject* object) const;

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

    /// Returns whether this thread is waiting on objects from a WaitSynchronization call.
    bool IsSleepingOnWait() const {
        return status == ThreadStatus::WaitSynch;
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

    void SetProcessorID(s32 new_core) {
        processor_id = new_core;
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
        for (const auto& waiting_object : wait_objects) {
            waiting_object->RemoveWaitingThread(this);
        }
        wait_objects.clear();
    }

    /// Determines whether all the objects this thread is waiting on are ready.
    bool AllWaitObjectsReady() const;

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

    ThreadActivity GetActivity() const {
        return activity;
    }

    void SetActivity(ThreadActivity value);

    /// Sleeps this thread for the given amount of nanoseconds.
    void Sleep(s64 nanoseconds);

    /// Yields this thread without rebalancing loads.
    bool YieldSimple();

    /// Yields this thread and does a load rebalancing.
    bool YieldAndBalanceLoad();

    /// Yields this thread and if the core is left idle, loads are rebalanced
    bool YieldAndWaitForLoadBalancing();

    void IncrementYieldCount() {
        yield_count++;
    }

    u64 GetYieldCount() const {
        return yield_count;
    }

    ThreadSchedStatus GetSchedulingStatus() const {
        return static_cast<ThreadSchedStatus>(scheduling_state &
                                              static_cast<u32>(ThreadSchedMasks::LowMask));
    }

    bool IsRunning() const {
        return is_running;
    }

    void SetIsRunning(bool value) {
        is_running = value;
    }

private:
    explicit Thread(KernelCore& kernel);
    ~Thread() override;

    void SetSchedulingStatus(ThreadSchedStatus new_status);
    void SetCurrentPriority(u32 new_priority);
    ResultCode SetCoreAndAffinityMask(s32 new_core, u64 new_affinity_mask);

    void AdjustSchedulingOnStatus(u32 old_flags);
    void AdjustSchedulingOnPriority(u32 old_priority);
    void AdjustSchedulingOnAffinity(u64 old_affinity_mask, s32 old_core);

    Core::ARM_Interface::ThreadContext context{};

    u64 thread_id = 0;

    ThreadStatus status = ThreadStatus::Dormant;

    VAddr entry_point = 0;
    VAddr stack_top = 0;

    /// Nominal thread priority, as set by the emulated application.
    /// The nominal priority is the thread priority without priority
    /// inheritance taken into account.
    u32 nominal_priority = 0;

    /// Current thread priority. This may change over the course of the
    /// thread's lifetime in order to facilitate priority inheritance.
    u32 current_priority = 0;

    u64 total_cpu_time_ticks = 0; ///< Total CPU running ticks.
    u64 last_running_ticks = 0;   ///< CPU tick when thread was last running
    u64 yield_count = 0;          ///< Number of redundant yields carried by this thread.
                                  ///< a redundant yield is one where no scheduling is changed

    s32 processor_id = 0;

    VAddr tls_address = 0; ///< Virtual address of the Thread Local Storage of the thread
    u64 tpidr_el0 = 0;     ///< TPIDR_EL0 read/write system register.

    /// Process that owns this thread
    Process* owner_process;

    /// Objects that the thread is waiting on, in the same order as they were
    /// passed to WaitSynchronization.
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

    /// Handle used as userdata to reference this object when inserting into the CoreTiming queue.
    Handle callback_handle = 0;

    /// Callback that will be invoked when the thread is resumed from a waiting state. If the thread
    /// was waiting via WaitSynchronization then the object will be the last object that became
    /// available. In case of a timeout, the object will be nullptr.
    WakeupCallback wakeup_callback;

    Scheduler* scheduler = nullptr;

    u32 ideal_core{0xFFFFFFFF};
    u64 affinity_mask{0x1};

    ThreadActivity activity = ThreadActivity::Normal;

    s32 ideal_core_override = -1;
    u64 affinity_mask_override = 0x1;
    u32 affinity_override_count = 0;

    u32 scheduling_state = 0;
    bool is_running = false;

    std::string name;
};

/**
 * Gets the current thread
 */
Thread* GetCurrentThread();

} // namespace Kernel
