// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <memory>
#include <random>
#include "common/assert.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/file_sys/program_metadata.h"
#include "core/hle/kernel/errors.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/resource_limit.h"
#include "core/hle/kernel/scheduler.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/kernel/vm_manager.h"
#include "core/memory.h"
#include "core/settings.h"

namespace Kernel {
namespace {
/**
 * Sets up the primary application thread
 *
 * @param owner_process The parent process for the main thread
 * @param kernel The kernel instance to create the main thread under.
 * @param entry_point The address at which the thread should start execution
 * @param priority The priority to give the main thread
 */
void SetupMainThread(Process& owner_process, KernelCore& kernel, VAddr entry_point, u32 priority) {
    // Setup page table so we can write to memory
    SetCurrentPageTable(&owner_process.VMManager().page_table);

    // Initialize new "main" thread
    const VAddr stack_top = owner_process.VMManager().GetTLSIORegionEndAddress();
    auto thread_res = Thread::Create(kernel, "main", entry_point, priority, 0,
                                     owner_process.GetIdealCore(), stack_top, owner_process);

    SharedPtr<Thread> thread = std::move(thread_res).Unwrap();

    // Register 1 must be a handle to the main thread
    const Handle guest_handle = owner_process.GetHandleTable().Create(thread).Unwrap();
    thread->SetGuestHandle(guest_handle);
    thread->GetContext().cpu_registers[1] = guest_handle;

    // Threads by default are dormant, wake up the main thread so it runs when the scheduler fires
    thread->ResumeFromWait();
}
} // Anonymous namespace

CodeSet::CodeSet() = default;
CodeSet::~CodeSet() = default;

SharedPtr<Process> Process::Create(KernelCore& kernel, std::string&& name) {
    SharedPtr<Process> process(new Process(kernel));

    process->name = std::move(name);
    process->resource_limit = kernel.GetSystemResourceLimit();
    process->status = ProcessStatus::Created;
    process->program_id = 0;
    process->process_id = kernel.CreateNewProcessID();
    process->capabilities.InitializeForMetadatalessProcess();

    std::mt19937 rng(Settings::values.rng_seed.value_or(0));
    std::uniform_int_distribution<u64> distribution;
    std::generate(process->random_entropy.begin(), process->random_entropy.end(),
                  [&] { return distribution(rng); });

    kernel.AppendNewProcess(process);
    return process;
}

SharedPtr<ResourceLimit> Process::GetResourceLimit() const {
    return resource_limit;
}

ResultCode Process::ClearSignalState() {
    if (status == ProcessStatus::Exited) {
        LOG_ERROR(Kernel, "called on a terminated process instance.");
        return ERR_INVALID_STATE;
    }

    if (!is_signaled) {
        LOG_ERROR(Kernel, "called on a process instance that isn't signaled.");
        return ERR_INVALID_STATE;
    }

    is_signaled = false;
    return RESULT_SUCCESS;
}

ResultCode Process::LoadFromMetadata(const FileSys::ProgramMetadata& metadata) {
    program_id = metadata.GetTitleID();
    ideal_core = metadata.GetMainThreadCore();
    is_64bit_process = metadata.Is64BitProgram();

    vm_manager.Reset(metadata.GetAddressSpaceType());

    const auto& caps = metadata.GetKernelCapabilities();
    const auto capability_init_result =
        capabilities.InitializeForUserProcess(caps.data(), caps.size(), vm_manager);
    if (capability_init_result.IsError()) {
        return capability_init_result;
    }

    return handle_table.SetSize(capabilities.GetHandleTableSize());
}

void Process::Run(VAddr entry_point, s32 main_thread_priority, u32 stack_size) {
    // Allocate and map the main thread stack
    // TODO(bunnei): This is heap area that should be allocated by the kernel and not mapped as part
    // of the user address space.
    vm_manager
        .MapMemoryBlock(vm_manager.GetTLSIORegionEndAddress() - stack_size,
                        std::make_shared<std::vector<u8>>(stack_size, 0), 0, stack_size,
                        MemoryState::Stack)
        .Unwrap();

    vm_manager.LogLayout();
    ChangeStatus(ProcessStatus::Running);

    SetupMainThread(*this, kernel, entry_point, main_thread_priority);
}

void Process::PrepareForTermination() {
    ChangeStatus(ProcessStatus::Exiting);

    const auto stop_threads = [this](const std::vector<SharedPtr<Thread>>& thread_list) {
        for (auto& thread : thread_list) {
            if (thread->GetOwnerProcess() != this)
                continue;

            if (thread == GetCurrentThread())
                continue;

            // TODO(Subv): When are the other running/ready threads terminated?
            ASSERT_MSG(thread->GetStatus() == ThreadStatus::WaitSynchAny ||
                           thread->GetStatus() == ThreadStatus::WaitSynchAll,
                       "Exiting processes with non-waiting threads is currently unimplemented");

            thread->Stop();
        }
    };

    const auto& system = Core::System::GetInstance();
    stop_threads(system.Scheduler(0).GetThreadList());
    stop_threads(system.Scheduler(1).GetThreadList());
    stop_threads(system.Scheduler(2).GetThreadList());
    stop_threads(system.Scheduler(3).GetThreadList());

    ChangeStatus(ProcessStatus::Exited);
}

/**
 * Finds a free location for the TLS section of a thread.
 * @param tls_slots The TLS page array of the thread's owner process.
 * Returns a tuple of (page, slot, alloc_needed) where:
 * page: The index of the first allocated TLS page that has free slots.
 * slot: The index of the first free slot in the indicated page.
 * alloc_needed: Whether there's a need to allocate a new TLS page (All pages are full).
 */
static std::tuple<std::size_t, std::size_t, bool> FindFreeThreadLocalSlot(
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

VAddr Process::MarkNextAvailableTLSSlotAsUsed(Thread& thread) {
    auto [available_page, available_slot, needs_allocation] = FindFreeThreadLocalSlot(tls_slots);
    const VAddr tls_begin = vm_manager.GetTLSIORegionBaseAddress();

    if (needs_allocation) {
        tls_slots.emplace_back(0); // The page is completely available at the start
        available_page = tls_slots.size() - 1;
        available_slot = 0; // Use the first slot in the new page

        // Allocate some memory from the end of the linear heap for this region.
        auto& tls_memory = thread.GetTLSMemory();
        tls_memory->insert(tls_memory->end(), Memory::PAGE_SIZE, 0);

        vm_manager.RefreshMemoryBlockMappings(tls_memory.get());

        vm_manager.MapMemoryBlock(tls_begin + available_page * Memory::PAGE_SIZE, tls_memory, 0,
                                  Memory::PAGE_SIZE, MemoryState::ThreadLocal);
    }

    tls_slots[available_page].set(available_slot);

    return tls_begin + available_page * Memory::PAGE_SIZE + available_slot * Memory::TLS_ENTRY_SIZE;
}

void Process::FreeTLSSlot(VAddr tls_address) {
    const VAddr tls_base = tls_address - vm_manager.GetTLSIORegionBaseAddress();
    const VAddr tls_page = tls_base / Memory::PAGE_SIZE;
    const VAddr tls_slot = (tls_base % Memory::PAGE_SIZE) / Memory::TLS_ENTRY_SIZE;

    tls_slots[tls_page].reset(tls_slot);
}

void Process::LoadModule(CodeSet module_, VAddr base_addr) {
    const auto MapSegment = [&](CodeSet::Segment& segment, VMAPermission permissions,
                                MemoryState memory_state) {
        const auto vma = vm_manager
                             .MapMemoryBlock(segment.addr + base_addr, module_.memory,
                                             segment.offset, segment.size, memory_state)
                             .Unwrap();
        vm_manager.Reprotect(vma, permissions);
    };

    // Map CodeSet segments
    MapSegment(module_.CodeSegment(), VMAPermission::ReadExecute, MemoryState::CodeStatic);
    MapSegment(module_.RODataSegment(), VMAPermission::Read, MemoryState::CodeMutable);
    MapSegment(module_.DataSegment(), VMAPermission::ReadWrite, MemoryState::CodeMutable);

    // Clear instruction cache in CPU JIT
    Core::System::GetInstance().ArmInterface(0).ClearInstructionCache();
    Core::System::GetInstance().ArmInterface(1).ClearInstructionCache();
    Core::System::GetInstance().ArmInterface(2).ClearInstructionCache();
    Core::System::GetInstance().ArmInterface(3).ClearInstructionCache();
}

Kernel::Process::Process(KernelCore& kernel) : WaitObject{kernel} {}
Kernel::Process::~Process() {}

void Process::Acquire(Thread* thread) {
    ASSERT_MSG(!ShouldWait(thread), "Object unavailable!");
}

bool Process::ShouldWait(Thread* thread) const {
    return !is_signaled;
}

void Process::ChangeStatus(ProcessStatus new_status) {
    if (status == new_status) {
        return;
    }

    status = new_status;
    is_signaled = true;
    WakeupAllWaitingThreads();
}

} // namespace Kernel
