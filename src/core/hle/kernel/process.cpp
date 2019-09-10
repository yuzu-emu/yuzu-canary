// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <bitset>
#include <memory>
#include <random>
#include "common/alignment.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/file_sys/program_metadata.h"
#include "core/hle/kernel/code_set.h"
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
 * @param priority The priority to give the main thread
 */
void SetupMainThread(Process& owner_process, KernelCore& kernel, u32 priority) {
    const auto& vm_manager = owner_process.VMManager();
    const VAddr entry_point = vm_manager.GetCodeRegionBaseAddress();
    const VAddr stack_top = vm_manager.GetTLSIORegionEndAddress();
    auto thread_res = Thread::Create(kernel, "main", entry_point, priority, 0,
                                     owner_process.GetIdealCore(), stack_top, owner_process);

    SharedPtr<Thread> thread = std::move(thread_res).Unwrap();

    // Register 1 must be a handle to the main thread
    const Handle thread_handle = owner_process.GetHandleTable().Create(thread).Unwrap();
    thread->GetContext().cpu_registers[1] = thread_handle;

    // Threads by default are dormant, wake up the main thread so it runs when the scheduler fires
    thread->ResumeFromWait();
}
} // Anonymous namespace

// Represents a page used for thread-local storage.
//
// Each TLS page contains slots that may be used by processes and threads.
// Every process and thread is created with a slot in some arbitrary page
// (whichever page happens to have an available slot).
class TLSPage {
public:
    static constexpr std::size_t num_slot_entries = Memory::PAGE_SIZE / Memory::TLS_ENTRY_SIZE;

    explicit TLSPage(VAddr address) : base_address{address} {}

    bool HasAvailableSlots() const {
        return !is_slot_used.all();
    }

    VAddr GetBaseAddress() const {
        return base_address;
    }

    std::optional<VAddr> ReserveSlot() {
        for (std::size_t i = 0; i < is_slot_used.size(); i++) {
            if (is_slot_used[i]) {
                continue;
            }

            is_slot_used[i] = true;
            return base_address + (i * Memory::TLS_ENTRY_SIZE);
        }

        return std::nullopt;
    }

    void ReleaseSlot(VAddr address) {
        // Ensure that all given addresses are consistent with how TLS pages
        // are intended to be used when releasing slots.
        ASSERT(IsWithinPage(address));
        ASSERT((address % Memory::TLS_ENTRY_SIZE) == 0);

        const std::size_t index = (address - base_address) / Memory::TLS_ENTRY_SIZE;
        is_slot_used[index] = false;
    }

private:
    bool IsWithinPage(VAddr address) const {
        return base_address <= address && address < base_address + Memory::PAGE_SIZE;
    }

    VAddr base_address;
    std::bitset<num_slot_entries> is_slot_used;
};

SharedPtr<Process> Process::Create(Core::System& system, std::string name, ProcessType type) {
    auto& kernel = system.Kernel();

    SharedPtr<Process> process(new Process(system));
    process->name = std::move(name);
    process->resource_limit = kernel.GetSystemResourceLimit();
    process->status = ProcessStatus::Created;
    process->program_id = 0;
    process->process_id = type == ProcessType::KernelInternal ? kernel.CreateNewKernelProcessID()
                                                              : kernel.CreateNewUserProcessID();
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

u64 Process::GetTotalPhysicalMemoryAvailable() const {
    return vm_manager.GetTotalPhysicalMemoryAvailable();
}

u64 Process::GetTotalPhysicalMemoryAvailableWithoutSystemResource() const {
    return GetTotalPhysicalMemoryAvailable() - GetSystemResourceSize();
}

u64 Process::GetTotalPhysicalMemoryUsed() const {
    return vm_manager.GetCurrentHeapSize() + main_thread_stack_size + code_memory_size +
           GetSystemResourceUsage();
}

u64 Process::GetTotalPhysicalMemoryUsedWithoutSystemResource() const {
    return GetTotalPhysicalMemoryUsed() - GetSystemResourceUsage();
}

void Process::RegisterThread(const Thread* thread) {
    thread_list.push_back(thread);
}

void Process::UnregisterThread(const Thread* thread) {
    thread_list.remove(thread);
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
    system_resource_size = metadata.GetSystemResourceSize();

    vm_manager.Reset(metadata.GetAddressSpaceType());

    const auto& caps = metadata.GetKernelCapabilities();
    const auto capability_init_result =
        capabilities.InitializeForUserProcess(caps.data(), caps.size(), vm_manager);
    if (capability_init_result.IsError()) {
        return capability_init_result;
    }

    return handle_table.SetSize(capabilities.GetHandleTableSize());
}

void Process::Run(s32 main_thread_priority, u64 stack_size) {
    AllocateMainThreadStack(stack_size);
    tls_region_address = CreateTLSRegion();

    vm_manager.LogLayout();

    ChangeStatus(ProcessStatus::Running);

    SetupMainThread(*this, kernel, main_thread_priority);
}

void Process::PrepareForTermination() {
    ChangeStatus(ProcessStatus::Exiting);

    const auto stop_threads = [this](const std::vector<SharedPtr<Thread>>& thread_list) {
        for (auto& thread : thread_list) {
            if (thread->GetOwnerProcess() != this)
                continue;

            if (thread == system.CurrentScheduler().GetCurrentThread())
                continue;

            // TODO(Subv): When are the other running/ready threads terminated?
            ASSERT_MSG(thread->GetStatus() == ThreadStatus::WaitSynch,
                       "Exiting processes with non-waiting threads is currently unimplemented");

            thread->Stop();
        }
    };

    stop_threads(system.GlobalScheduler().GetThreadList());

    FreeTLSRegion(tls_region_address);
    tls_region_address = 0;

    ChangeStatus(ProcessStatus::Exited);
}

/**
 * Attempts to find a TLS page that contains a free slot for
 * use by a thread.
 *
 * @returns If a page with an available slot is found, then an iterator
 *          pointing to the page is returned. Otherwise the end iterator
 *          is returned instead.
 */
static auto FindTLSPageWithAvailableSlots(std::vector<TLSPage>& tls_pages) {
    return std::find_if(tls_pages.begin(), tls_pages.end(),
                        [](const auto& page) { return page.HasAvailableSlots(); });
}

VAddr Process::CreateTLSRegion() {
    auto tls_page_iter = FindTLSPageWithAvailableSlots(tls_pages);

    if (tls_page_iter == tls_pages.cend()) {
        const auto region_address =
            vm_manager.FindFreeRegion(vm_manager.GetTLSIORegionBaseAddress(),
                                      vm_manager.GetTLSIORegionEndAddress(), Memory::PAGE_SIZE);
        ASSERT(region_address.Succeeded());

        const auto map_result = vm_manager.MapMemoryBlock(
            *region_address, std::make_shared<PhysicalMemory>(Memory::PAGE_SIZE), 0,
            Memory::PAGE_SIZE, MemoryState::ThreadLocal);
        ASSERT(map_result.Succeeded());

        tls_pages.emplace_back(*region_address);

        const auto reserve_result = tls_pages.back().ReserveSlot();
        ASSERT(reserve_result.has_value());

        return *reserve_result;
    }

    return *tls_page_iter->ReserveSlot();
}

void Process::FreeTLSRegion(VAddr tls_address) {
    const VAddr aligned_address = Common::AlignDown(tls_address, Memory::PAGE_SIZE);
    auto iter =
        std::find_if(tls_pages.begin(), tls_pages.end(), [aligned_address](const auto& page) {
            return page.GetBaseAddress() == aligned_address;
        });

    // Something has gone very wrong if we're freeing a region
    // with no actual page available.
    ASSERT(iter != tls_pages.cend());

    iter->ReleaseSlot(tls_address);
}

void Process::LoadModule(CodeSet module_, VAddr base_addr) {
    const auto memory = std::make_shared<PhysicalMemory>(std::move(module_.memory));

    const auto MapSegment = [&](const CodeSet::Segment& segment, VMAPermission permissions,
                                MemoryState memory_state) {
        const auto vma = vm_manager
                             .MapMemoryBlock(segment.addr + base_addr, memory, segment.offset,
                                             segment.size, memory_state)
                             .Unwrap();
        vm_manager.Reprotect(vma, permissions);
    };

    // Map CodeSet segments
    MapSegment(module_.CodeSegment(), VMAPermission::ReadExecute, MemoryState::Code);
    MapSegment(module_.RODataSegment(), VMAPermission::Read, MemoryState::CodeData);
    MapSegment(module_.DataSegment(), VMAPermission::ReadWrite, MemoryState::CodeData);

    code_memory_size += module_.memory.size();
}

Process::Process(Core::System& system)
    : WaitObject{system.Kernel()}, vm_manager{system},
      address_arbiter{system}, mutex{system}, system{system} {}

Process::~Process() = default;

void Process::Acquire(Thread* thread) {
    ASSERT_MSG(!ShouldWait(thread), "Object unavailable!");
}

bool Process::ShouldWait(const Thread* thread) const {
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

void Process::AllocateMainThreadStack(u64 stack_size) {
    // The kernel always ensures that the given stack size is page aligned.
    main_thread_stack_size = Common::AlignUp(stack_size, Memory::PAGE_SIZE);

    // Allocate and map the main thread stack
    const VAddr mapping_address = vm_manager.GetTLSIORegionEndAddress() - main_thread_stack_size;
    vm_manager
        .MapMemoryBlock(mapping_address, std::make_shared<PhysicalMemory>(main_thread_stack_size),
                        0, main_thread_stack_size, MemoryState::Stack)
        .Unwrap();
}

} // namespace Kernel
