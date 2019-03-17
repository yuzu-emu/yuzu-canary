// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <iterator>
#include <utility>
#include "common/assert.h"
#include "common/logging/log.h"
#include "common/memory_hook.h"
#include "core/arm/arm_interface.h"
#include "core/core.h"
#include "core/file_sys/program_metadata.h"
#include "core/hle/kernel/errors.h"
#include "core/hle/kernel/vm_manager.h"
#include "core/memory.h"
#include "core/memory_setup.h"

namespace Kernel {
namespace {
const char* GetMemoryStateName(MemoryState state) {
    static constexpr const char* names[] = {
        "Unmapped",         "Io",
        "Normal",           "CodeStatic",
        "CodeMutable",      "Heap",
        "Shared",           "Unknown1",
        "ModuleCodeStatic", "ModuleCodeMutable",
        "IpcBuffer0",       "Stack",
        "ThreadLocal",      "TransferMemoryIsolated",
        "TransferMemory",   "ProcessMemory",
        "Inaccessible",     "IpcBuffer1",
        "IpcBuffer3",       "KernelStack",
    };

    return names[ToSvcMemoryState(state)];
}

// Checks if a given address range lies within a larger address range.
constexpr bool IsInsideAddressRange(VAddr address, u64 size, VAddr address_range_begin,
                                    VAddr address_range_end) {
    const VAddr end_address = address + size - 1;
    return address_range_begin <= address && end_address <= address_range_end - 1;
}
} // Anonymous namespace

bool VirtualMemoryArea::CanBeMergedWith(const VirtualMemoryArea& next) const {
    ASSERT(base + size == next.base);
    if (permissions != next.permissions || state != next.state || attribute != next.attribute ||
        type != next.type) {
        return false;
    }
    if (type == VMAType::AllocatedMemoryBlock &&
        (backing_block != next.backing_block || offset + size != next.offset)) {
        return false;
    }
    if (type == VMAType::BackingMemory && backing_memory + size != next.backing_memory) {
        return false;
    }
    if (type == VMAType::MMIO && paddr + size != next.paddr) {
        return false;
    }
    return true;
}

VMManager::VMManager() {
    // Default to assuming a 39-bit address space. This way we have a sane
    // starting point with executables that don't provide metadata.
    Reset(FileSys::ProgramAddressSpaceType::Is39Bit);
}

VMManager::~VMManager() {
    Reset(FileSys::ProgramAddressSpaceType::Is39Bit);
}

void VMManager::Reset(FileSys::ProgramAddressSpaceType type) {
    Clear();

    InitializeMemoryRegionRanges(type);

    page_table.Resize(address_space_width);

    // Initialize the map with a single free region covering the entire managed space.
    VirtualMemoryArea initial_vma;
    initial_vma.size = address_space_end;
    vma_map.emplace(initial_vma.base, initial_vma);

    UpdatePageTableForVMA(initial_vma);
}

VMManager::VMAHandle VMManager::FindVMA(VAddr target) const {
    if (target >= address_space_end) {
        return vma_map.end();
    } else {
        return std::prev(vma_map.upper_bound(target));
    }
}

bool VMManager::IsValidHandle(VMAHandle handle) const {
    return handle != vma_map.cend();
}

ResultVal<VMManager::VMAHandle> VMManager::MapMemoryBlock(VAddr target,
                                                          std::shared_ptr<std::vector<u8>> block,
                                                          std::size_t offset, u64 size,
                                                          MemoryState state) {
    ASSERT(block != nullptr);
    ASSERT(offset + size <= block->size());

    // This is the appropriately sized VMA that will turn into our allocation.
    CASCADE_RESULT(VMAIter vma_handle, CarveVMA(target, size));
    VirtualMemoryArea& final_vma = vma_handle->second;
    ASSERT(final_vma.size == size);

    auto& system = Core::System::GetInstance();
    system.ArmInterface(0).MapBackingMemory(target, size, block->data() + offset,
                                            VMAPermission::ReadWriteExecute);
    system.ArmInterface(1).MapBackingMemory(target, size, block->data() + offset,
                                            VMAPermission::ReadWriteExecute);
    system.ArmInterface(2).MapBackingMemory(target, size, block->data() + offset,
                                            VMAPermission::ReadWriteExecute);
    system.ArmInterface(3).MapBackingMemory(target, size, block->data() + offset,
                                            VMAPermission::ReadWriteExecute);

    final_vma.type = VMAType::AllocatedMemoryBlock;
    final_vma.permissions = VMAPermission::ReadWrite;
    final_vma.state = state;
    final_vma.backing_block = std::move(block);
    final_vma.offset = offset;
    UpdatePageTableForVMA(final_vma);

    return MakeResult<VMAHandle>(MergeAdjacent(vma_handle));
}

ResultVal<VMManager::VMAHandle> VMManager::MapBackingMemory(VAddr target, u8* memory, u64 size,
                                                            MemoryState state) {
    ASSERT(memory != nullptr);

    // This is the appropriately sized VMA that will turn into our allocation.
    CASCADE_RESULT(VMAIter vma_handle, CarveVMA(target, size));
    VirtualMemoryArea& final_vma = vma_handle->second;
    ASSERT(final_vma.size == size);

    auto& system = Core::System::GetInstance();
    system.ArmInterface(0).MapBackingMemory(target, size, memory, VMAPermission::ReadWriteExecute);
    system.ArmInterface(1).MapBackingMemory(target, size, memory, VMAPermission::ReadWriteExecute);
    system.ArmInterface(2).MapBackingMemory(target, size, memory, VMAPermission::ReadWriteExecute);
    system.ArmInterface(3).MapBackingMemory(target, size, memory, VMAPermission::ReadWriteExecute);

    final_vma.type = VMAType::BackingMemory;
    final_vma.permissions = VMAPermission::ReadWrite;
    final_vma.state = state;
    final_vma.backing_memory = memory;
    UpdatePageTableForVMA(final_vma);

    return MakeResult<VMAHandle>(MergeAdjacent(vma_handle));
}

ResultVal<VAddr> VMManager::FindFreeRegion(u64 size) const {
    // Find the first Free VMA.
    const VAddr base = GetASLRRegionBaseAddress();
    const VMAHandle vma_handle = std::find_if(vma_map.begin(), vma_map.end(), [&](const auto& vma) {
        if (vma.second.type != VMAType::Free)
            return false;

        const VAddr vma_end = vma.second.base + vma.second.size;
        return vma_end > base && vma_end >= base + size;
    });

    if (vma_handle == vma_map.end()) {
        // TODO(Subv): Find the correct error code here.
        return ResultCode(-1);
    }

    const VAddr target = std::max(base, vma_handle->second.base);
    return MakeResult<VAddr>(target);
}

ResultVal<VMManager::VMAHandle> VMManager::MapMMIO(VAddr target, PAddr paddr, u64 size,
                                                   MemoryState state,
                                                   Common::MemoryHookPointer mmio_handler) {
    // This is the appropriately sized VMA that will turn into our allocation.
    CASCADE_RESULT(VMAIter vma_handle, CarveVMA(target, size));
    VirtualMemoryArea& final_vma = vma_handle->second;
    ASSERT(final_vma.size == size);

    final_vma.type = VMAType::MMIO;
    final_vma.permissions = VMAPermission::ReadWrite;
    final_vma.state = state;
    final_vma.paddr = paddr;
    final_vma.mmio_handler = std::move(mmio_handler);
    UpdatePageTableForVMA(final_vma);

    return MakeResult<VMAHandle>(MergeAdjacent(vma_handle));
}

VMManager::VMAIter VMManager::Unmap(VMAIter vma_handle) {
    VirtualMemoryArea& vma = vma_handle->second;
    vma.type = VMAType::Free;
    vma.permissions = VMAPermission::None;
    vma.state = MemoryState::Unmapped;
    vma.attribute = MemoryAttribute::None;

    vma.backing_block = nullptr;
    vma.offset = 0;
    vma.backing_memory = nullptr;
    vma.paddr = 0;

    UpdatePageTableForVMA(vma);

    return MergeAdjacent(vma_handle);
}

ResultCode VMManager::UnmapRange(VAddr target, u64 size) {
    CASCADE_RESULT(VMAIter vma, CarveVMARange(target, size));
    const VAddr target_end = target + size;

    const VMAIter end = vma_map.end();
    // The comparison against the end of the range must be done using addresses since VMAs can be
    // merged during this process, causing invalidation of the iterators.
    while (vma != end && vma->second.base < target_end) {
        vma = std::next(Unmap(vma));
    }

    ASSERT(FindVMA(target)->second.size >= size);

    auto& system = Core::System::GetInstance();
    system.ArmInterface(0).UnmapMemory(target, size);
    system.ArmInterface(1).UnmapMemory(target, size);
    system.ArmInterface(2).UnmapMemory(target, size);
    system.ArmInterface(3).UnmapMemory(target, size);

    return RESULT_SUCCESS;
}

VMManager::VMAHandle VMManager::Reprotect(VMAHandle vma_handle, VMAPermission new_perms) {
    VMAIter iter = StripIterConstness(vma_handle);

    VirtualMemoryArea& vma = iter->second;
    vma.permissions = new_perms;
    UpdatePageTableForVMA(vma);

    return MergeAdjacent(iter);
}

ResultCode VMManager::ReprotectRange(VAddr target, u64 size, VMAPermission new_perms) {
    CASCADE_RESULT(VMAIter vma, CarveVMARange(target, size));
    const VAddr target_end = target + size;

    const VMAIter end = vma_map.end();
    // The comparison against the end of the range must be done using addresses since VMAs can be
    // merged during this process, causing invalidation of the iterators.
    while (vma != end && vma->second.base < target_end) {
        vma = std::next(StripIterConstness(Reprotect(vma, new_perms)));
    }

    return RESULT_SUCCESS;
}

ResultVal<VAddr> VMManager::HeapAllocate(VAddr target, u64 size, VMAPermission perms) {
    if (!IsWithinHeapRegion(target, size)) {
        return ERR_INVALID_ADDRESS;
    }

    if (heap_memory == nullptr) {
        // Initialize heap
        heap_memory = std::make_shared<std::vector<u8>>();
        heap_start = heap_end = target;
    } else {
        UnmapRange(heap_start, heap_end - heap_start);
    }

    // If necessary, expand backing vector to cover new heap extents.
    if (target < heap_start) {
        heap_memory->insert(begin(*heap_memory), heap_start - target, 0);
        heap_start = target;
        RefreshMemoryBlockMappings(heap_memory.get());
    }
    if (target + size > heap_end) {
        heap_memory->insert(end(*heap_memory), (target + size) - heap_end, 0);
        heap_end = target + size;
        RefreshMemoryBlockMappings(heap_memory.get());
    }
    ASSERT(heap_end - heap_start == heap_memory->size());

    CASCADE_RESULT(auto vma, MapMemoryBlock(target, heap_memory, target - heap_start, size,
                                            MemoryState::Heap));
    Reprotect(vma, perms);

    heap_used = size;

    return MakeResult<VAddr>(heap_end - size);
}

ResultCode VMManager::HeapFree(VAddr target, u64 size) {
    if (!IsWithinHeapRegion(target, size)) {
        return ERR_INVALID_ADDRESS;
    }

    if (size == 0) {
        return RESULT_SUCCESS;
    }

    const ResultCode result = UnmapRange(target, size);
    if (result.IsError()) {
        return result;
    }

    heap_used -= size;
    return RESULT_SUCCESS;
}

MemoryInfo VMManager::QueryMemory(VAddr address) const {
    const auto vma = FindVMA(address);
    MemoryInfo memory_info{};

    if (IsValidHandle(vma)) {
        memory_info.base_address = vma->second.base;
        memory_info.attributes = ToSvcMemoryAttribute(vma->second.attribute);
        memory_info.permission = static_cast<u32>(vma->second.permissions);
        memory_info.size = vma->second.size;
        memory_info.state = ToSvcMemoryState(vma->second.state);
    } else {
        memory_info.base_address = address_space_end;
        memory_info.permission = static_cast<u32>(VMAPermission::None);
        memory_info.size = 0 - address_space_end;
        memory_info.state = static_cast<u32>(MemoryState::Inaccessible);
    }

    return memory_info;
}

ResultCode VMManager::SetMemoryAttribute(VAddr address, u64 size, MemoryAttribute mask,
                                         MemoryAttribute attribute) {
    constexpr auto ignore_mask = MemoryAttribute::Uncached | MemoryAttribute::DeviceMapped;
    constexpr auto attribute_mask = ~ignore_mask;

    const auto result = CheckRangeState(
        address, size, MemoryState::FlagUncached, MemoryState::FlagUncached, VMAPermission::None,
        VMAPermission::None, attribute_mask, MemoryAttribute::None, ignore_mask);

    if (result.Failed()) {
        return result.Code();
    }

    const auto [prev_state, prev_permissions, prev_attributes] = *result;
    const auto new_attribute = (prev_attributes & ~mask) | (mask & attribute);

    const auto carve_result = CarveVMARange(address, size);
    if (carve_result.Failed()) {
        return carve_result.Code();
    }

    auto vma_iter = *carve_result;
    vma_iter->second.attribute = new_attribute;

    MergeAdjacent(vma_iter);
    return RESULT_SUCCESS;
}

ResultCode VMManager::MirrorMemory(VAddr dst_addr, VAddr src_addr, u64 size, MemoryState state) {
    const auto vma = FindVMA(src_addr);

    ASSERT_MSG(vma != vma_map.end(), "Invalid memory address");
    ASSERT_MSG(vma->second.backing_block, "Backing block doesn't exist for address");

    // The returned VMA might be a bigger one encompassing the desired address.
    const auto vma_offset = src_addr - vma->first;
    ASSERT_MSG(vma_offset + size <= vma->second.size,
               "Shared memory exceeds bounds of mapped block");

    const std::shared_ptr<std::vector<u8>>& backing_block = vma->second.backing_block;
    const std::size_t backing_block_offset = vma->second.offset + vma_offset;

    CASCADE_RESULT(auto new_vma,
                   MapMemoryBlock(dst_addr, backing_block, backing_block_offset, size, state));
    // Protect mirror with permissions from old region
    Reprotect(new_vma, vma->second.permissions);
    // Remove permissions from old region
    Reprotect(vma, VMAPermission::None);

    return RESULT_SUCCESS;
}

void VMManager::RefreshMemoryBlockMappings(const std::vector<u8>* block) {
    // If this ever proves to have a noticeable performance impact, allow users of the function to
    // specify a specific range of addresses to limit the scan to.
    for (const auto& p : vma_map) {
        const VirtualMemoryArea& vma = p.second;
        if (block == vma.backing_block.get()) {
            UpdatePageTableForVMA(vma);
        }
    }
}

void VMManager::LogLayout() const {
    for (const auto& p : vma_map) {
        const VirtualMemoryArea& vma = p.second;
        LOG_DEBUG(Kernel, "{:016X} - {:016X} size: {:016X} {}{}{} {}", vma.base,
                  vma.base + vma.size, vma.size,
                  (u8)vma.permissions & (u8)VMAPermission::Read ? 'R' : '-',
                  (u8)vma.permissions & (u8)VMAPermission::Write ? 'W' : '-',
                  (u8)vma.permissions & (u8)VMAPermission::Execute ? 'X' : '-',
                  GetMemoryStateName(vma.state));
    }
}

VMManager::VMAIter VMManager::StripIterConstness(const VMAHandle& iter) {
    // This uses a neat C++ trick to convert a const_iterator to a regular iterator, given
    // non-const access to its container.
    return vma_map.erase(iter, iter); // Erases an empty range of elements
}

ResultVal<VMManager::VMAIter> VMManager::CarveVMA(VAddr base, u64 size) {
    ASSERT_MSG((size & Memory::PAGE_MASK) == 0, "non-page aligned size: 0x{:016X}", size);
    ASSERT_MSG((base & Memory::PAGE_MASK) == 0, "non-page aligned base: 0x{:016X}", base);

    VMAIter vma_handle = StripIterConstness(FindVMA(base));
    if (vma_handle == vma_map.end()) {
        // Target address is outside the range managed by the kernel
        return ERR_INVALID_ADDRESS;
    }

    const VirtualMemoryArea& vma = vma_handle->second;
    if (vma.type != VMAType::Free) {
        // Region is already allocated
        return ERR_INVALID_ADDRESS_STATE;
    }

    const VAddr start_in_vma = base - vma.base;
    const VAddr end_in_vma = start_in_vma + size;

    if (end_in_vma > vma.size) {
        // Requested allocation doesn't fit inside VMA
        return ERR_INVALID_ADDRESS_STATE;
    }

    if (end_in_vma != vma.size) {
        // Split VMA at the end of the allocated region
        SplitVMA(vma_handle, end_in_vma);
    }
    if (start_in_vma != 0) {
        // Split VMA at the start of the allocated region
        vma_handle = SplitVMA(vma_handle, start_in_vma);
    }

    return MakeResult<VMAIter>(vma_handle);
}

ResultVal<VMManager::VMAIter> VMManager::CarveVMARange(VAddr target, u64 size) {
    ASSERT_MSG((size & Memory::PAGE_MASK) == 0, "non-page aligned size: 0x{:016X}", size);
    ASSERT_MSG((target & Memory::PAGE_MASK) == 0, "non-page aligned base: 0x{:016X}", target);

    const VAddr target_end = target + size;
    ASSERT(target_end >= target);
    ASSERT(target_end <= address_space_end);
    ASSERT(size > 0);

    VMAIter begin_vma = StripIterConstness(FindVMA(target));
    const VMAIter i_end = vma_map.lower_bound(target_end);
    if (std::any_of(begin_vma, i_end,
                    [](const auto& entry) { return entry.second.type == VMAType::Free; })) {
        return ERR_INVALID_ADDRESS_STATE;
    }

    if (target != begin_vma->second.base) {
        begin_vma = SplitVMA(begin_vma, target - begin_vma->second.base);
    }

    VMAIter end_vma = StripIterConstness(FindVMA(target_end));
    if (end_vma != vma_map.end() && target_end != end_vma->second.base) {
        end_vma = SplitVMA(end_vma, target_end - end_vma->second.base);
    }

    return MakeResult<VMAIter>(begin_vma);
}

VMManager::VMAIter VMManager::SplitVMA(VMAIter vma_handle, u64 offset_in_vma) {
    VirtualMemoryArea& old_vma = vma_handle->second;
    VirtualMemoryArea new_vma = old_vma; // Make a copy of the VMA

    // For now, don't allow no-op VMA splits (trying to split at a boundary) because it's probably
    // a bug. This restriction might be removed later.
    ASSERT(offset_in_vma < old_vma.size);
    ASSERT(offset_in_vma > 0);

    old_vma.size = offset_in_vma;
    new_vma.base += offset_in_vma;
    new_vma.size -= offset_in_vma;

    switch (new_vma.type) {
    case VMAType::Free:
        break;
    case VMAType::AllocatedMemoryBlock:
        new_vma.offset += offset_in_vma;
        break;
    case VMAType::BackingMemory:
        new_vma.backing_memory += offset_in_vma;
        break;
    case VMAType::MMIO:
        new_vma.paddr += offset_in_vma;
        break;
    }

    ASSERT(old_vma.CanBeMergedWith(new_vma));

    return vma_map.emplace_hint(std::next(vma_handle), new_vma.base, new_vma);
}

VMManager::VMAIter VMManager::MergeAdjacent(VMAIter iter) {
    const VMAIter next_vma = std::next(iter);
    if (next_vma != vma_map.end() && iter->second.CanBeMergedWith(next_vma->second)) {
        iter->second.size += next_vma->second.size;
        vma_map.erase(next_vma);
    }

    if (iter != vma_map.begin()) {
        VMAIter prev_vma = std::prev(iter);
        if (prev_vma->second.CanBeMergedWith(iter->second)) {
            prev_vma->second.size += iter->second.size;
            vma_map.erase(iter);
            iter = prev_vma;
        }
    }

    return iter;
}

void VMManager::UpdatePageTableForVMA(const VirtualMemoryArea& vma) {
    switch (vma.type) {
    case VMAType::Free:
        Memory::UnmapRegion(page_table, vma.base, vma.size);
        break;
    case VMAType::AllocatedMemoryBlock:
        Memory::MapMemoryRegion(page_table, vma.base, vma.size,
                                vma.backing_block->data() + vma.offset);
        break;
    case VMAType::BackingMemory:
        Memory::MapMemoryRegion(page_table, vma.base, vma.size, vma.backing_memory);
        break;
    case VMAType::MMIO:
        Memory::MapIoRegion(page_table, vma.base, vma.size, vma.mmio_handler);
        break;
    }
}

void VMManager::InitializeMemoryRegionRanges(FileSys::ProgramAddressSpaceType type) {
    u64 map_region_size = 0;
    u64 heap_region_size = 0;
    u64 new_map_region_size = 0;
    u64 tls_io_region_size = 0;

    switch (type) {
    case FileSys::ProgramAddressSpaceType::Is32Bit:
    case FileSys::ProgramAddressSpaceType::Is32BitNoMap:
        address_space_width = 32;
        code_region_base = 0x200000;
        code_region_end = code_region_base + 0x3FE00000;
        aslr_region_base = 0x200000;
        aslr_region_end = aslr_region_base + 0xFFE00000;
        if (type == FileSys::ProgramAddressSpaceType::Is32Bit) {
            map_region_size = 0x40000000;
            heap_region_size = 0x40000000;
        } else {
            map_region_size = 0;
            heap_region_size = 0x80000000;
        }
        break;
    case FileSys::ProgramAddressSpaceType::Is36Bit:
        address_space_width = 36;
        code_region_base = 0x8000000;
        code_region_end = code_region_base + 0x78000000;
        aslr_region_base = 0x8000000;
        aslr_region_end = aslr_region_base + 0xFF8000000;
        map_region_size = 0x180000000;
        heap_region_size = 0x180000000;
        break;
    case FileSys::ProgramAddressSpaceType::Is39Bit:
        address_space_width = 39;
        code_region_base = 0x8000000;
        code_region_end = code_region_base + 0x80000000;
        aslr_region_base = 0x8000000;
        aslr_region_end = aslr_region_base + 0x7FF8000000;
        map_region_size = 0x1000000000;
        heap_region_size = 0x180000000;
        new_map_region_size = 0x80000000;
        tls_io_region_size = 0x1000000000;
        break;
    default:
        UNREACHABLE_MSG("Invalid address space type specified: {}", static_cast<u32>(type));
        return;
    }

    address_space_base = 0;
    address_space_end = 1ULL << address_space_width;

    map_region_base = code_region_end;
    map_region_end = map_region_base + map_region_size;

    heap_region_base = map_region_end;
    heap_region_end = heap_region_base + heap_region_size;

    new_map_region_base = heap_region_end;
    new_map_region_end = new_map_region_base + new_map_region_size;

    tls_io_region_base = new_map_region_end;
    tls_io_region_end = tls_io_region_base + tls_io_region_size;

    if (new_map_region_size == 0) {
        new_map_region_base = address_space_base;
        new_map_region_end = address_space_end;
    }
}

void VMManager::Clear() {
    ClearVMAMap();
    ClearPageTable();
}

void VMManager::ClearVMAMap() {
    vma_map.clear();
}

void VMManager::ClearPageTable() {
    std::fill(page_table.pointers.begin(), page_table.pointers.end(), nullptr);
    page_table.special_regions.clear();
    std::fill(page_table.attributes.begin(), page_table.attributes.end(),
              Common::PageType::Unmapped);
}

VMManager::CheckResults VMManager::CheckRangeState(VAddr address, u64 size, MemoryState state_mask,
                                                   MemoryState state, VMAPermission permission_mask,
                                                   VMAPermission permissions,
                                                   MemoryAttribute attribute_mask,
                                                   MemoryAttribute attribute,
                                                   MemoryAttribute ignore_mask) const {
    auto iter = FindVMA(address);

    // If we don't have a valid VMA handle at this point, then it means this is
    // being called with an address outside of the address space, which is definitely
    // indicative of a bug, as this function only operates on mapped memory regions.
    DEBUG_ASSERT(IsValidHandle(iter));

    const VAddr end_address = address + size - 1;
    const MemoryAttribute initial_attributes = iter->second.attribute;
    const VMAPermission initial_permissions = iter->second.permissions;
    const MemoryState initial_state = iter->second.state;

    while (true) {
        // The iterator should be valid throughout the traversal. Hitting the end of
        // the mapped VMA regions is unquestionably indicative of a bug.
        DEBUG_ASSERT(IsValidHandle(iter));

        const auto& vma = iter->second;

        if (vma.state != initial_state) {
            return ERR_INVALID_ADDRESS_STATE;
        }

        if ((vma.state & state_mask) != state) {
            return ERR_INVALID_ADDRESS_STATE;
        }

        if (vma.permissions != initial_permissions) {
            return ERR_INVALID_ADDRESS_STATE;
        }

        if ((vma.permissions & permission_mask) != permissions) {
            return ERR_INVALID_ADDRESS_STATE;
        }

        if ((vma.attribute | ignore_mask) != (initial_attributes | ignore_mask)) {
            return ERR_INVALID_ADDRESS_STATE;
        }

        if ((vma.attribute & attribute_mask) != attribute) {
            return ERR_INVALID_ADDRESS_STATE;
        }

        if (end_address <= vma.EndAddress()) {
            break;
        }

        ++iter;
    }

    return MakeResult(
        std::make_tuple(initial_state, initial_permissions, initial_attributes & ~ignore_mask));
}

u64 VMManager::GetTotalMemoryUsage() const {
    LOG_WARNING(Kernel, "(STUBBED) called");
    return 0xF8000000;
}

u64 VMManager::GetTotalHeapUsage() const {
    return heap_used;
}

VAddr VMManager::GetAddressSpaceBaseAddress() const {
    return address_space_base;
}

VAddr VMManager::GetAddressSpaceEndAddress() const {
    return address_space_end;
}

u64 VMManager::GetAddressSpaceSize() const {
    return address_space_end - address_space_base;
}

u64 VMManager::GetAddressSpaceWidth() const {
    return address_space_width;
}

bool VMManager::IsWithinAddressSpace(VAddr address, u64 size) const {
    return IsInsideAddressRange(address, size, GetAddressSpaceBaseAddress(),
                                GetAddressSpaceEndAddress());
}

VAddr VMManager::GetASLRRegionBaseAddress() const {
    return aslr_region_base;
}

VAddr VMManager::GetASLRRegionEndAddress() const {
    return aslr_region_end;
}

u64 VMManager::GetASLRRegionSize() const {
    return aslr_region_end - aslr_region_base;
}

bool VMManager::IsWithinASLRRegion(VAddr begin, u64 size) const {
    const VAddr range_end = begin + size;
    const VAddr aslr_start = GetASLRRegionBaseAddress();
    const VAddr aslr_end = GetASLRRegionEndAddress();

    if (aslr_start > begin || begin > range_end || range_end - 1 > aslr_end - 1) {
        return false;
    }

    if (range_end > heap_region_base && heap_region_end > begin) {
        return false;
    }

    if (range_end > map_region_base && map_region_end > begin) {
        return false;
    }

    return true;
}

VAddr VMManager::GetCodeRegionBaseAddress() const {
    return code_region_base;
}

VAddr VMManager::GetCodeRegionEndAddress() const {
    return code_region_end;
}

u64 VMManager::GetCodeRegionSize() const {
    return code_region_end - code_region_base;
}

bool VMManager::IsWithinCodeRegion(VAddr address, u64 size) const {
    return IsInsideAddressRange(address, size, GetCodeRegionBaseAddress(),
                                GetCodeRegionEndAddress());
}

VAddr VMManager::GetHeapRegionBaseAddress() const {
    return heap_region_base;
}

VAddr VMManager::GetHeapRegionEndAddress() const {
    return heap_region_end;
}

u64 VMManager::GetHeapRegionSize() const {
    return heap_region_end - heap_region_base;
}

bool VMManager::IsWithinHeapRegion(VAddr address, u64 size) const {
    return IsInsideAddressRange(address, size, GetHeapRegionBaseAddress(),
                                GetHeapRegionEndAddress());
}

VAddr VMManager::GetMapRegionBaseAddress() const {
    return map_region_base;
}

VAddr VMManager::GetMapRegionEndAddress() const {
    return map_region_end;
}

u64 VMManager::GetMapRegionSize() const {
    return map_region_end - map_region_base;
}

bool VMManager::IsWithinMapRegion(VAddr address, u64 size) const {
    return IsInsideAddressRange(address, size, GetMapRegionBaseAddress(), GetMapRegionEndAddress());
}

VAddr VMManager::GetNewMapRegionBaseAddress() const {
    return new_map_region_base;
}

VAddr VMManager::GetNewMapRegionEndAddress() const {
    return new_map_region_end;
}

u64 VMManager::GetNewMapRegionSize() const {
    return new_map_region_end - new_map_region_base;
}

bool VMManager::IsWithinNewMapRegion(VAddr address, u64 size) const {
    return IsInsideAddressRange(address, size, GetNewMapRegionBaseAddress(),
                                GetNewMapRegionEndAddress());
}

VAddr VMManager::GetTLSIORegionBaseAddress() const {
    return tls_io_region_base;
}

VAddr VMManager::GetTLSIORegionEndAddress() const {
    return tls_io_region_end;
}

u64 VMManager::GetTLSIORegionSize() const {
    return tls_io_region_end - tls_io_region_base;
}

bool VMManager::IsWithinTLSIORegion(VAddr address, u64 size) const {
    return IsInsideAddressRange(address, size, GetTLSIORegionBaseAddress(),
                                GetTLSIORegionEndAddress());
}

} // namespace Kernel
