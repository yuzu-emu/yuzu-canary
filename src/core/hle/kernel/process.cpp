// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <memory>
#include "common/assert.h"
#include "common/common_funcs.h"
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

namespace Kernel {

CodeSet::CodeSet() = default;
CodeSet::~CodeSet() = default;

SharedPtr<Process> Process::Create(KernelCore& kernel, std::string&& name) {
    SharedPtr<Process> process(new Process(kernel));

    process->name = std::move(name);
    process->flags.raw = 0;
    process->flags.memory_region.Assign(MemoryRegion::APPLICATION);
    process->resource_limit = kernel.ResourceLimitForCategory(ResourceLimitCategory::APPLICATION);
    process->status = ProcessStatus::Created;
    process->program_id = 0;
    process->process_id = kernel.CreateNewProcessID();
    process->svc_access_mask.set();

    kernel.AppendNewProcess(process);
    return process;
}

void Process::LoadFromMetadata(const FileSys::ProgramMetadata& metadata) {
    program_id = metadata.GetTitleID();
    is_64bit_process = metadata.Is64BitProgram();
    vm_manager.Reset(metadata.GetAddressSpaceType());
}

void Process::ParseKernelCaps(const u32* kernel_caps, std::size_t len) {
    for (std::size_t i = 0; i < len; ++i) {
        u32 descriptor = kernel_caps[i];
        u32 type = descriptor >> 20;

        if (descriptor == 0xFFFFFFFF) {
            // Unused descriptor entry
            continue;
        } else if ((type & 0xF00) == 0xE00) { // 0x0FFF
            // Allowed interrupts list
            LOG_WARNING(Loader, "ExHeader allowed interrupts list ignored");
        } else if ((type & 0xF80) == 0xF00) { // 0x07FF
            // Allowed syscalls mask
            unsigned int index = ((descriptor >> 24) & 7) * 24;
            u32 bits = descriptor & 0xFFFFFF;

            while (bits && index < svc_access_mask.size()) {
                svc_access_mask.set(index, bits & 1);
                ++index;
                bits >>= 1;
            }
        } else if ((type & 0xFF0) == 0xFE0) { // 0x00FF
            // Handle table size
            handle_table_size = descriptor & 0x3FF;
        } else if ((type & 0xFF8) == 0xFF0) { // 0x007F
            // Misc. flags
            flags.raw = descriptor & 0xFFFF;
        } else if ((type & 0xFFE) == 0xFF8) { // 0x001F
            // Mapped memory range
            if (i + 1 >= len || ((kernel_caps[i + 1] >> 20) & 0xFFE) != 0xFF8) {
                LOG_WARNING(Loader, "Incomplete exheader memory range descriptor ignored.");
                continue;
            }
            u32 end_desc = kernel_caps[i + 1];
            ++i; // Skip over the second descriptor on the next iteration

            AddressMapping mapping;
            mapping.address = descriptor << 12;
            VAddr end_address = end_desc << 12;

            if (mapping.address < end_address) {
                mapping.size = end_address - mapping.address;
            } else {
                mapping.size = 0;
            }

            mapping.read_only = (descriptor & (1 << 20)) != 0;
            mapping.unk_flag = (end_desc & (1 << 20)) != 0;

            address_mappings.push_back(mapping);
        } else if ((type & 0xFFF) == 0xFFE) { // 0x000F
            // Mapped memory page
            AddressMapping mapping;
            mapping.address = descriptor << 12;
            mapping.size = Memory::PAGE_SIZE;
            mapping.read_only = false;
            mapping.unk_flag = false;

            address_mappings.push_back(mapping);
        } else if ((type & 0xFE0) == 0xFC0) { // 0x01FF
            // Kernel version
            kernel_version = descriptor & 0xFFFF;

            int minor = kernel_version & 0xFF;
            int major = (kernel_version >> 8) & 0xFF;
            LOG_INFO(Loader, "ExHeader kernel version: {}.{}", major, minor);
        } else {
            LOG_ERROR(Loader, "Unhandled kernel caps descriptor: 0x{:08X}", descriptor);
        }
    }
}

void Process::Run(VAddr entry_point, s32 main_thread_priority, u32 stack_size) {
    // Allocate and map the main thread stack
    // TODO(bunnei): This is heap area that should be allocated by the kernel and not mapped as part
    // of the user address space.
    vm_manager
        .MapMemoryBlock(vm_manager.GetTLSIORegionEndAddress() - stack_size,
                        std::make_shared<std::vector<u8>>(stack_size, 0), 0, stack_size,
                        MemoryState::Mapped)
        .Unwrap();

    vm_manager.LogLayout();
    status = ProcessStatus::Running;

    Kernel::SetupMainThread(kernel, entry_point, main_thread_priority, *this);
}

void Process::PrepareForTermination() {
    status = ProcessStatus::Exited;

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

ResultVal<VAddr> Process::HeapAllocate(VAddr target, u64 size, VMAPermission perms) {
    if (target < vm_manager.GetHeapRegionBaseAddress() ||
        target + size > vm_manager.GetHeapRegionEndAddress() || target + size < target) {
        return ERR_INVALID_ADDRESS;
    }

    if (heap_memory == nullptr) {
        // Initialize heap
        heap_memory = std::make_shared<std::vector<u8>>();
        heap_start = heap_end = target;
    } else {
        vm_manager.UnmapRange(heap_start, heap_end - heap_start);
    }

    // If necessary, expand backing vector to cover new heap extents.
    if (target < heap_start) {
        heap_memory->insert(begin(*heap_memory), heap_start - target, 0);
        heap_start = target;
        vm_manager.RefreshMemoryBlockMappings(heap_memory.get());
    }
    if (target + size > heap_end) {
        heap_memory->insert(end(*heap_memory), (target + size) - heap_end, 0);
        heap_end = target + size;
        vm_manager.RefreshMemoryBlockMappings(heap_memory.get());
    }
    ASSERT(heap_end - heap_start == heap_memory->size());

    CASCADE_RESULT(auto vma, vm_manager.MapMemoryBlock(target, heap_memory, target - heap_start,
                                                       size, MemoryState::Heap));
    vm_manager.Reprotect(vma, perms);

    heap_used = size;

    return MakeResult<VAddr>(heap_end - size);
}

ResultCode Process::HeapFree(VAddr target, u32 size) {
    if (target < vm_manager.GetHeapRegionBaseAddress() ||
        target + size > vm_manager.GetHeapRegionEndAddress() || target + size < target) {
        return ERR_INVALID_ADDRESS;
    }

    if (size == 0) {
        return RESULT_SUCCESS;
    }

    ResultCode result = vm_manager.UnmapRange(target, size);
    if (result.IsError())
        return result;

    heap_used -= size;

    return RESULT_SUCCESS;
}

ResultCode Process::MirrorMemory(VAddr dst_addr, VAddr src_addr, u64 size) {
    auto vma = vm_manager.FindVMA(src_addr);

    ASSERT_MSG(vma != vm_manager.vma_map.end(), "Invalid memory address");
    ASSERT_MSG(vma->second.backing_block, "Backing block doesn't exist for address");

    // The returned VMA might be a bigger one encompassing the desired address.
    auto vma_offset = src_addr - vma->first;
    ASSERT_MSG(vma_offset + size <= vma->second.size,
               "Shared memory exceeds bounds of mapped block");

    const std::shared_ptr<std::vector<u8>>& backing_block = vma->second.backing_block;
    std::size_t backing_block_offset = vma->second.offset + vma_offset;

    CASCADE_RESULT(auto new_vma,
                   vm_manager.MapMemoryBlock(dst_addr, backing_block, backing_block_offset, size,
                                             MemoryState::Mapped));
    // Protect mirror with permissions from old region
    vm_manager.Reprotect(new_vma, vma->second.permissions);
    // Remove permissions from old region
    vm_manager.Reprotect(vma, VMAPermission::None);

    return RESULT_SUCCESS;
}

ResultCode Process::UnmapMemory(VAddr dst_addr, VAddr /*src_addr*/, u64 size) {
    return vm_manager.UnmapRange(dst_addr, size);
}

Kernel::Process::Process(KernelCore& kernel) : Object{kernel} {}
Kernel::Process::~Process() {}

} // namespace Kernel
