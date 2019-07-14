// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <list>
#include <string>
#include <vector>
#include "common/common_types.h"
#include "core/hle/kernel/address_arbiter.h"
#include "core/hle/kernel/handle_table.h"
#include "core/hle/kernel/mutex.h"
#include "core/hle/kernel/process_capability.h"
#include "core/hle/kernel/vm_manager.h"
#include "core/hle/kernel/wait_object.h"
#include "core/hle/result.h"

namespace Core {
class System;
}

namespace FileSys {
class ProgramMetadata;
}

namespace Kernel {

class KernelCore;
class ResourceLimit;
class Thread;
class TLSPage;

struct CodeSet;

enum class MemoryRegion : u16 {
    APPLICATION = 1,
    SYSTEM = 2,
    BASE = 3,
};

/**
 * Indicates the status of a Process instance.
 *
 * @note These match the values as used by kernel,
 *       so new entries should only be added if RE
 *       shows that a new value has been introduced.
 */
enum class ProcessStatus {
    Created,
    CreatedWithDebuggerAttached,
    Running,
    WaitingForDebuggerToAttach,
    DebuggerAttached,
    Exiting,
    Exited,
    DebugBreak,
};

class Process final : public WaitObject {
public:
    enum : u64 {
        /// Lowest allowed process ID for a kernel initial process.
        InitialKIPIDMin = 1,
        /// Highest allowed process ID for a kernel initial process.
        InitialKIPIDMax = 80,

        /// Lowest allowed process ID for a userland process.
        ProcessIDMin = 81,
        /// Highest allowed process ID for a userland process.
        ProcessIDMax = 0xFFFFFFFFFFFFFFFF,
    };

    // Used to determine how process IDs are assigned.
    enum class ProcessType {
        KernelInternal,
        Userland,
    };

    static constexpr std::size_t RANDOM_ENTROPY_SIZE = 4;

    static SharedPtr<Process> Create(Core::System& system, std::string name, ProcessType type);

    std::string GetTypeName() const override {
        return "Process";
    }
    std::string GetName() const override {
        return name;
    }

    static constexpr HandleType HANDLE_TYPE = HandleType::Process;
    HandleType GetHandleType() const override {
        return HANDLE_TYPE;
    }

    /// Gets a reference to the process' memory manager.
    Kernel::VMManager& VMManager() {
        return vm_manager;
    }

    /// Gets a const reference to the process' memory manager.
    const Kernel::VMManager& VMManager() const {
        return vm_manager;
    }

    /// Gets a reference to the process' handle table.
    HandleTable& GetHandleTable() {
        return handle_table;
    }

    /// Gets a const reference to the process' handle table.
    const HandleTable& GetHandleTable() const {
        return handle_table;
    }

    /// Gets a reference to the process' address arbiter.
    AddressArbiter& GetAddressArbiter() {
        return address_arbiter;
    }

    /// Gets a const reference to the process' address arbiter.
    const AddressArbiter& GetAddressArbiter() const {
        return address_arbiter;
    }

    /// Gets a reference to the process' mutex lock.
    Mutex& GetMutex() {
        return mutex;
    }

    /// Gets a const reference to the process' mutex lock
    const Mutex& GetMutex() const {
        return mutex;
    }

    /// Gets the address to the process' dedicated TLS region.
    VAddr GetTLSRegionAddress() const {
        return tls_region_address;
    }

    /// Gets the current status of the process
    ProcessStatus GetStatus() const {
        return status;
    }

    /// Gets the unique ID that identifies this particular process.
    u64 GetProcessID() const {
        return process_id;
    }

    /// Gets the title ID corresponding to this process.
    u64 GetTitleID() const {
        return program_id;
    }

    /// Gets the resource limit descriptor for this process
    SharedPtr<ResourceLimit> GetResourceLimit() const;

    /// Gets the ideal CPU core ID for this process
    u8 GetIdealCore() const {
        return ideal_core;
    }

    /// Gets the bitmask of allowed cores that this process' threads can run on.
    u64 GetCoreMask() const {
        return capabilities.GetCoreMask();
    }

    /// Gets the bitmask of allowed thread priorities.
    u64 GetPriorityMask() const {
        return capabilities.GetPriorityMask();
    }

    /// Gets the amount of secure memory to allocate for memory management.
    u32 GetSystemResourceSize() const {
        return system_resource_size;
    }

    /// Gets the amount of secure memory currently in use for memory management.
    u32 GetSystemResourceUsage() const {
        // On hardware, this returns the amount of system resource memory that has
        // been used by the kernel. This is problematic for Yuzu to emulate, because
        // system resource memory is used for page tables -- and yuzu doesn't really
        // have a way to calculate how much memory is required for page tables for
        // the current process at any given time.
        // TODO: Is this even worth implementing? Games may retrieve this value via
        // an SDK function that gets used + available system resource size for debug
        // or diagnostic purposes. However, it seems unlikely that a game would make
        // decisions based on how much system memory is dedicated to its page tables.
        // Is returning a value other than zero wise?
        return 0;
    }

    /// Whether this process is an AArch64 or AArch32 process.
    bool Is64BitProcess() const {
        return is_64bit_process;
    }

    /// Gets the total running time of the process instance in ticks.
    u64 GetCPUTimeTicks() const {
        return total_process_running_time_ticks;
    }

    /// Updates the total running time, adding the given ticks to it.
    void UpdateCPUTimeTicks(u64 ticks) {
        total_process_running_time_ticks += ticks;
    }

    /// Gets 8 bytes of random data for svcGetInfo RandomEntropy
    u64 GetRandomEntropy(std::size_t index) const {
        return random_entropy.at(index);
    }

    /// Retrieves the total physical memory available to this process in bytes.
    u64 GetTotalPhysicalMemoryAvailable() const;

    /// Retrieves the total physical memory available to this process in bytes,
    /// without the size of the personal system resource heap added to it.
    u64 GetTotalPhysicalMemoryAvailableWithoutSystemResource() const;

    /// Retrieves the total physical memory used by this process in bytes.
    u64 GetTotalPhysicalMemoryUsed() const;

    /// Retrieves the total physical memory used by this process in bytes,
    /// without the size of the personal system resource heap added to it.
    u64 GetTotalPhysicalMemoryUsedWithoutSystemResource() const;

    /// Gets the list of all threads created with this process as their owner.
    const std::list<const Thread*>& GetThreadList() const {
        return thread_list;
    }

    /// Registers a thread as being created under this process,
    /// adding it to this process' thread list.
    void RegisterThread(const Thread* thread);

    /// Unregisters a thread from this process, removing it
    /// from this process' thread list.
    void UnregisterThread(const Thread* thread);

    /// Clears the signaled state of the process if and only if it's signaled.
    ///
    /// @pre The process must not be already terminated. If this is called on a
    ///      terminated process, then ERR_INVALID_STATE will be returned.
    ///
    /// @pre The process must be in a signaled state. If this is called on a
    ///      process instance that is not signaled, ERR_INVALID_STATE will be
    ///      returned.
    ResultCode ClearSignalState();

    /**
     * Loads process-specifics configuration info with metadata provided
     * by an executable.
     *
     * @param metadata The provided metadata to load process specific info from.
     *
     * @returns RESULT_SUCCESS if all relevant metadata was able to be
     *          loaded and parsed. Otherwise, an error code is returned.
     */
    ResultCode LoadFromMetadata(const FileSys::ProgramMetadata& metadata);

    /**
     * Starts the main application thread for this process.
     *
     * @param main_thread_priority The priority for the main thread.
     * @param stack_size           The stack size for the main thread in bytes.
     */
    void Run(s32 main_thread_priority, u64 stack_size);

    /**
     * Prepares a process for termination by stopping all of its threads
     * and clearing any other resources.
     */
    void PrepareForTermination();

    void LoadModule(CodeSet module_, VAddr base_addr);

    ///////////////////////////////////////////////////////////////////////////////////////////////
    // Thread-local storage management

    // Marks the next available region as used and returns the address of the slot.
    [[nodiscard]] VAddr CreateTLSRegion();

    // Frees a used TLS slot identified by the given address
    void FreeTLSRegion(VAddr tls_address);

private:
    explicit Process(Core::System& system);
    ~Process() override;

    /// Checks if the specified thread should wait until this process is available.
    bool ShouldWait(const Thread* thread) const override;

    /// Acquires/locks this process for the specified thread if it's available.
    void Acquire(Thread* thread) override;

    /// Changes the process status. If the status is different
    /// from the current process status, then this will trigger
    /// a process signal.
    void ChangeStatus(ProcessStatus new_status);

    /// Allocates the main thread stack for the process, given the stack size in bytes.
    void AllocateMainThreadStack(u64 stack_size);

    /// Memory manager for this process.
    Kernel::VMManager vm_manager;

    /// Size of the main thread's stack in bytes.
    u64 main_thread_stack_size = 0;

    /// Size of the loaded code memory in bytes.
    u64 code_memory_size = 0;

    /// Current status of the process
    ProcessStatus status{};

    /// The ID of this process
    u64 process_id = 0;

    /// Title ID corresponding to the process
    u64 program_id = 0;

    /// Specifies additional memory to be reserved for the process's memory management by the
    /// system. When this is non-zero, secure memory is allocated and used for page table allocation
    /// instead of using the normal global page tables/memory block management.
    u32 system_resource_size = 0;

    /// Resource limit descriptor for this process
    SharedPtr<ResourceLimit> resource_limit;

    /// The ideal CPU core for this process, threads are scheduled on this core by default.
    u8 ideal_core = 0;

    /// The Thread Local Storage area is allocated as processes create threads,
    /// each TLS area is 0x200 bytes, so one page (0x1000) is split up in 8 parts, and each part
    /// holds the TLS for a specific thread. This vector contains which parts are in use for each
    /// page as a bitmask.
    /// This vector will grow as more pages are allocated for new threads.
    std::vector<TLSPage> tls_pages;

    /// Contains the parsed process capability descriptors.
    ProcessCapabilities capabilities;

    /// Whether or not this process is AArch64, or AArch32.
    /// By default, we currently assume this is true, unless otherwise
    /// specified by metadata provided to the process during loading.
    bool is_64bit_process = true;

    /// Whether or not this process is signaled. This occurs
    /// upon the process changing to a different state.
    bool is_signaled = false;

    /// Total running time for the process in ticks.
    u64 total_process_running_time_ticks = 0;

    /// Per-process handle table for storing created object handles in.
    HandleTable handle_table;

    /// Per-process address arbiter.
    AddressArbiter address_arbiter;

    /// The per-process mutex lock instance used for handling various
    /// forms of services, such as lock arbitration, and condition
    /// variable related facilities.
    Mutex mutex;

    /// Address indicating the location of the process' dedicated TLS region.
    VAddr tls_region_address = 0;

    /// Random values for svcGetInfo RandomEntropy
    std::array<u64, RANDOM_ENTROPY_SIZE> random_entropy{};

    /// List of threads that are running with this process as their owner.
    std::list<const Thread*> thread_list;

    /// System context
    Core::System& system;

    /// Name of this process
    std::string name;
};

} // namespace Kernel
