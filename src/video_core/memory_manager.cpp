// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/alignment.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/memory.h"
#include "video_core/gpu.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_base.h"

namespace Tegra {

MemoryManager::MemoryManager() {
    std::fill(page_table.pointers.begin(), page_table.pointers.end(), nullptr);
    std::fill(page_table.attributes.begin(), page_table.attributes.end(),
              Common::PageType::Unmapped);
    page_table.Resize(address_space_width);

    // Initialize the map with a single free region covering the entire managed space.
    VirtualMemoryArea initial_vma;
    initial_vma.size = address_space_end;
    vma_map.emplace(initial_vma.base, initial_vma);

    UpdatePageTableForVMA(initial_vma);
}

GPUVAddr MemoryManager::AllocateSpace(u64 size, u64 align) {
    const GPUVAddr gpu_addr{
        FindFreeRegion(address_space_base, size, align, VirtualMemoryArea::Type::Unmapped)};
    AllocateMemory(gpu_addr, 0, size);
    return gpu_addr;
}

GPUVAddr MemoryManager::AllocateSpace(GPUVAddr gpu_addr, u64 size, u64 align) {
    AllocateMemory(gpu_addr, 0, size);
    return gpu_addr;
}

GPUVAddr MemoryManager::MapBufferEx(GPUVAddr cpu_addr, u64 size) {
    const GPUVAddr gpu_addr{
        FindFreeRegion(address_space_base, size, page_size, VirtualMemoryArea::Type::Unmapped)};
    MapBackingMemory(gpu_addr, Memory::GetPointer(cpu_addr), ((size + page_mask) & ~page_mask),
                     cpu_addr);
    return gpu_addr;
}

GPUVAddr MemoryManager::MapBufferEx(GPUVAddr cpu_addr, GPUVAddr gpu_addr, u64 size) {
    ASSERT((gpu_addr & page_mask) == 0);

    MapBackingMemory(gpu_addr, Memory::GetPointer(cpu_addr), ((size + page_mask) & ~page_mask),
                     cpu_addr);

    return gpu_addr;
}

GPUVAddr MemoryManager::UnmapBuffer(GPUVAddr gpu_addr, u64 size) {
    ASSERT((gpu_addr & page_mask) == 0);

    const CacheAddr cache_addr{ToCacheAddr(GetPointer(gpu_addr))};
    Core::System::GetInstance().Renderer().Rasterizer().FlushAndInvalidateRegion(cache_addr, size);

    UnmapRange(gpu_addr, ((size + page_mask) & ~page_mask));

    return gpu_addr;
}

GPUVAddr MemoryManager::FindFreeRegion(GPUVAddr region_start, u64 size, u64 align,
                                       VirtualMemoryArea::Type vma_type) {

    align = (align + page_mask) & ~page_mask;

    // Find the first Free VMA.
    const GPUVAddr base = region_start;
    const VMAHandle vma_handle = std::find_if(vma_map.begin(), vma_map.end(), [&](const auto& vma) {
        if (vma.second.type != vma_type)
            return false;

        const VAddr vma_end = vma.second.base + vma.second.size;
        return vma_end > base && vma_end >= base + size;
    });

    if (vma_handle == vma_map.end()) {
        return {};
    }

    return std::max(base, vma_handle->second.base);
}

bool MemoryManager::IsAddressValid(GPUVAddr addr) const {
    return (addr >> page_bits) < page_table.pointers.size();
}

std::optional<VAddr> MemoryManager::GpuToCpuAddress(GPUVAddr addr) {
    if (!IsAddressValid(addr)) {
        return {};
    }

    VAddr cpu_addr = page_table.backing_addr[addr >> page_bits];
    if (cpu_addr) {
        return cpu_addr + (addr & page_mask);
    }

    return {};
}

template <typename T>
T MemoryManager::Read(GPUVAddr addr) {
    if (!IsAddressValid(addr)) {
        return {};
    }

    const u8* page_pointer = page_table.pointers[addr >> page_bits];
    if (page_pointer) {
        // NOTE: Avoid adding any extra logic to this fast-path block
        T value;
        std::memcpy(&value, &page_pointer[addr & page_mask], sizeof(T));
        return value;
    }

    Common::PageType type = page_table.attributes[addr >> page_bits];
    switch (type) {
    case Common::PageType::Unmapped:
        LOG_ERROR(HW_GPU, "Unmapped Read{} @ 0x{:08X}", sizeof(T) * 8, addr);
        return 0;
    case Common::PageType::Memory:
        ASSERT_MSG(false, "Mapped memory page without a pointer @ {:016X}", addr);
        break;
    default:
        UNREACHABLE();
    }
    return {};
}

template <typename T>
void MemoryManager::Write(GPUVAddr addr, T data) {
    if (!IsAddressValid(addr)) {
        return;
    }

    u8* page_pointer = page_table.pointers[addr >> page_bits];
    if (page_pointer) {
        // NOTE: Avoid adding any extra logic to this fast-path block
        std::memcpy(&page_pointer[addr & page_mask], &data, sizeof(T));
        return;
    }

    Common::PageType type = page_table.attributes[addr >> page_bits];
    switch (type) {
    case Common::PageType::Unmapped:
        LOG_ERROR(HW_GPU, "Unmapped Write{} 0x{:08X} @ 0x{:016X}", sizeof(data) * 8,
                  static_cast<u32>(data), addr);
        return;
    case Common::PageType::Memory:
        ASSERT_MSG(false, "Mapped memory page without a pointer @ {:016X}", addr);
        break;
    default:
        UNREACHABLE();
    }
}

template u8 MemoryManager::Read<u8>(GPUVAddr addr);
template u16 MemoryManager::Read<u16>(GPUVAddr addr);
template u32 MemoryManager::Read<u32>(GPUVAddr addr);
template u64 MemoryManager::Read<u64>(GPUVAddr addr);
template void MemoryManager::Write<u8>(GPUVAddr addr, u8 data);
template void MemoryManager::Write<u16>(GPUVAddr addr, u16 data);
template void MemoryManager::Write<u32>(GPUVAddr addr, u32 data);
template void MemoryManager::Write<u64>(GPUVAddr addr, u64 data);

u8* MemoryManager::GetPointer(GPUVAddr addr) {
    if (!IsAddressValid(addr)) {
        return {};
    }

    u8* page_pointer = page_table.pointers[addr >> page_bits];
    if (page_pointer) {
        return page_pointer + (addr & page_mask);
    }

    LOG_ERROR(HW_GPU, "Unknown GetPointer @ 0x{:016X}", addr);
    return {};
}

void MemoryManager::ReadBlock(GPUVAddr src_addr, void* dest_buffer, std::size_t size) {
    std::memcpy(dest_buffer, GetPointer(src_addr), size);
}
void MemoryManager::WriteBlock(GPUVAddr dest_addr, const void* src_buffer, std::size_t size) {
    std::memcpy(GetPointer(dest_addr), src_buffer, size);
}

void MemoryManager::CopyBlock(GPUVAddr dest_addr, GPUVAddr src_addr, std::size_t size) {
    std::memcpy(GetPointer(dest_addr), GetPointer(src_addr), size);
}

void MemoryManager::MapPages(GPUVAddr base, u64 size, u8* memory, Common::PageType type,
                             VAddr backing_addr) {
    LOG_DEBUG(HW_GPU, "Mapping {} onto {:016X}-{:016X}", fmt::ptr(memory), base * page_size,
              (base + size) * page_size);

    VAddr end = base + size;
    ASSERT_MSG(end <= page_table.pointers.size(), "out of range mapping at {:016X}",
               base + page_table.pointers.size());

    std::fill(page_table.attributes.begin() + base, page_table.attributes.begin() + end, type);

    if (memory == nullptr) {
        std::fill(page_table.pointers.begin() + base, page_table.pointers.begin() + end, memory);
        std::fill(page_table.backing_addr.begin() + base, page_table.backing_addr.begin() + end,
                  backing_addr);
    } else {
        while (base != end) {
            page_table.pointers[base] = memory;
            page_table.backing_addr[base] = backing_addr;

            base += 1;
            memory += page_size;
            backing_addr += page_size;
        }
    }
}

void MemoryManager::MapMemoryRegion(GPUVAddr base, u64 size, u8* target, VAddr backing_addr) {
    ASSERT_MSG((size & page_mask) == 0, "non-page aligned size: {:016X}", size);
    ASSERT_MSG((base & page_mask) == 0, "non-page aligned base: {:016X}", base);
    MapPages(base / page_size, size / page_size, target, Common::PageType::Memory, backing_addr);
}

void MemoryManager::UnmapRegion(GPUVAddr base, u64 size) {
    ASSERT_MSG((size & page_mask) == 0, "non-page aligned size: {:016X}", size);
    ASSERT_MSG((base & page_mask) == 0, "non-page aligned base: {:016X}", base);
    MapPages(base / page_size, size / page_size, nullptr, Common::PageType::Unmapped);
}

bool VirtualMemoryArea::CanBeMergedWith(const VirtualMemoryArea& next) const {
    ASSERT(base + size == next.base);
    if (type != next.type) {
        return {};
    }
    if (type == VirtualMemoryArea::Type::Allocated && (offset + size != next.offset)) {
        return {};
    }
    if (type == VirtualMemoryArea::Type::Mapped && backing_memory + size != next.backing_memory) {
        return {};
    }
    return true;
}

MemoryManager::VMAHandle MemoryManager::FindVMA(GPUVAddr target) const {
    if (target >= address_space_end) {
        return vma_map.end();
    } else {
        return std::prev(vma_map.upper_bound(target));
    }
}

MemoryManager::VMAHandle MemoryManager::AllocateMemory(GPUVAddr target, std::size_t offset,
                                                       u64 size) {

    // This is the appropriately sized VMA that will turn into our allocation.
    VMAIter vma_handle = CarveVMA(target, size);
    VirtualMemoryArea& final_vma = vma_handle->second;
    ASSERT(final_vma.size == size);

    final_vma.type = VirtualMemoryArea::Type::Allocated;
    final_vma.offset = offset;
    UpdatePageTableForVMA(final_vma);

    return MergeAdjacent(vma_handle);
}

MemoryManager::VMAHandle MemoryManager::MapBackingMemory(GPUVAddr target, u8* memory, u64 size,
                                                         VAddr backing_addr) {
    // This is the appropriately sized VMA that will turn into our allocation.
    VMAIter vma_handle = CarveVMA(target, size);
    VirtualMemoryArea& final_vma = vma_handle->second;
    ASSERT(final_vma.size == size);

    final_vma.type = VirtualMemoryArea::Type::Mapped;
    final_vma.backing_memory = memory;
    final_vma.backing_addr = backing_addr;
    UpdatePageTableForVMA(final_vma);

    return MergeAdjacent(vma_handle);
}

MemoryManager::VMAIter MemoryManager::Unmap(VMAIter vma_handle) {
    VirtualMemoryArea& vma = vma_handle->second;
    vma.type = VirtualMemoryArea::Type::Allocated;
    vma.offset = 0;
    vma.backing_memory = nullptr;

    UpdatePageTableForVMA(vma);

    return MergeAdjacent(vma_handle);
}

void MemoryManager::UnmapRange(GPUVAddr target, u64 size) {
    VMAIter vma = CarveVMARange(target, size);
    const VAddr target_end = target + size;

    const VMAIter end = vma_map.end();
    // The comparison against the end of the range must be done using addresses since VMAs can be
    // merged during this process, causing invalidation of the iterators.
    while (vma != end && vma->second.base < target_end) {
        vma = std::next(Unmap(vma));
    }

    ASSERT(FindVMA(target)->second.size >= size);
}

MemoryManager::VMAIter MemoryManager::StripIterConstness(const VMAHandle& iter) {
    // This uses a neat C++ trick to convert a const_iterator to a regular iterator, given
    // non-const access to its container.
    return vma_map.erase(iter, iter); // Erases an empty range of elements
}

MemoryManager::VMAIter MemoryManager::CarveVMA(GPUVAddr base, u64 size) {
    ASSERT_MSG((size & Tegra::MemoryManager::page_mask) == 0, "non-page aligned size: 0x{:016X}",
               size);
    ASSERT_MSG((base & Tegra::MemoryManager::page_mask) == 0, "non-page aligned base: 0x{:016X}",
               base);

    VMAIter vma_handle = StripIterConstness(FindVMA(base));
    if (vma_handle == vma_map.end()) {
        // Target address is outside the range managed by the kernel
        return {};
    }

    const VirtualMemoryArea& vma = vma_handle->second;
    if (vma.type == VirtualMemoryArea::Type::Mapped) {
        // Region is already allocated
        return {};
    }

    const VAddr start_in_vma = base - vma.base;
    const VAddr end_in_vma = start_in_vma + size;

    if (end_in_vma < vma.size) {
        // Split VMA at the end of the allocated region
        SplitVMA(vma_handle, end_in_vma);
    }
    if (start_in_vma != 0) {
        // Split VMA at the start of the allocated region
        vma_handle = SplitVMA(vma_handle, start_in_vma);
    }

    return vma_handle;
}

MemoryManager::VMAIter MemoryManager::CarveVMARange(GPUVAddr target, u64 size) {
    ASSERT_MSG((size & Tegra::MemoryManager::page_mask) == 0, "non-page aligned size: 0x{:016X}",
               size);
    ASSERT_MSG((target & Tegra::MemoryManager::page_mask) == 0, "non-page aligned base: 0x{:016X}",
               target);

    const VAddr target_end = target + size;
    ASSERT(target_end >= target);
    ASSERT(size > 0);

    VMAIter begin_vma = StripIterConstness(FindVMA(target));
    const VMAIter i_end = vma_map.lower_bound(target_end);
    if (std::any_of(begin_vma, i_end, [](const auto& entry) {
            return entry.second.type == VirtualMemoryArea::Type::Unmapped;
        })) {
        return {};
    }

    if (target != begin_vma->second.base) {
        begin_vma = SplitVMA(begin_vma, target - begin_vma->second.base);
    }

    VMAIter end_vma = StripIterConstness(FindVMA(target_end));
    if (end_vma != vma_map.end() && target_end != end_vma->second.base) {
        end_vma = SplitVMA(end_vma, target_end - end_vma->second.base);
    }

    return begin_vma;
}

MemoryManager::VMAIter MemoryManager::SplitVMA(VMAIter vma_handle, u64 offset_in_vma) {
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
    case VirtualMemoryArea::Type::Unmapped:
        break;
    case VirtualMemoryArea::Type::Allocated:
        new_vma.offset += offset_in_vma;
        break;
    case VirtualMemoryArea::Type::Mapped:
        new_vma.backing_memory += offset_in_vma;
        break;
    }

    ASSERT(old_vma.CanBeMergedWith(new_vma));

    return vma_map.emplace_hint(std::next(vma_handle), new_vma.base, new_vma);
}

MemoryManager::VMAIter MemoryManager::MergeAdjacent(VMAIter iter) {
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

void MemoryManager::UpdatePageTableForVMA(const VirtualMemoryArea& vma) {
    switch (vma.type) {
    case VirtualMemoryArea::Type::Unmapped:
        UnmapRegion(vma.base, vma.size);
        break;
    case VirtualMemoryArea::Type::Allocated:
        MapMemoryRegion(vma.base, vma.size, nullptr, vma.backing_addr);
        break;
    case VirtualMemoryArea::Type::Mapped:
        MapMemoryRegion(vma.base, vma.size, vma.backing_memory, vma.backing_addr);
        break;
    }
}

} // namespace Tegra
