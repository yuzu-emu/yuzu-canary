// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/alignment.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "core/memory.h"
#include "video_core/memory_manager.h"

namespace Tegra {

MemoryManager::MemoryManager() {
    // Mark the first page as reserved, so that 0 is not a valid GPUVAddr. Otherwise, games might
    // try to use 0 as a valid address, which is also used to mean nullptr. This fixes a bug with
    // Undertale using 0 for a render target.
    PageSlot(0) = static_cast<u64>(PageStatus::Reserved);
}

GPUVAddr MemoryManager::AllocateSpace(u64 size, u64 align) {
    const std::optional<GPUVAddr> gpu_addr{FindFreeBlock(0, size, align, PageStatus::Unmapped)};

    ASSERT_MSG(gpu_addr, "unable to find available GPU memory");

    for (u64 offset{}; offset < size; offset += PAGE_SIZE) {
        VAddr& slot{PageSlot(*gpu_addr + offset)};

        ASSERT(slot == static_cast<u64>(PageStatus::Unmapped));

        slot = static_cast<u64>(PageStatus::Allocated);
    }

    return *gpu_addr;
}

GPUVAddr MemoryManager::AllocateSpace(GPUVAddr gpu_addr, u64 size, u64 align) {
    for (u64 offset{}; offset < size; offset += PAGE_SIZE) {
        VAddr& slot{PageSlot(gpu_addr + offset)};

        ASSERT(slot == static_cast<u64>(PageStatus::Unmapped));

        slot = static_cast<u64>(PageStatus::Allocated);
    }

    return gpu_addr;
}

GPUVAddr MemoryManager::MapBufferEx(VAddr cpu_addr, u64 size) {
    const std::optional<GPUVAddr> gpu_addr{FindFreeBlock(0, size, PAGE_SIZE, PageStatus::Unmapped)};

    ASSERT_MSG(gpu_addr, "unable to find available GPU memory");

    for (u64 offset{}; offset < size; offset += PAGE_SIZE) {
        VAddr& slot{PageSlot(*gpu_addr + offset)};

        ASSERT(slot == static_cast<u64>(PageStatus::Unmapped));

        slot = cpu_addr + offset;
    }

    const MappedRegion region{cpu_addr, *gpu_addr, size};
    mapped_regions.push_back(region);

    return *gpu_addr;
}

GPUVAddr MemoryManager::MapBufferEx(VAddr cpu_addr, GPUVAddr gpu_addr, u64 size) {
    ASSERT((gpu_addr & PAGE_MASK) == 0);

    if (PageSlot(gpu_addr) != static_cast<u64>(PageStatus::Allocated)) {
        // Page has been already mapped. In this case, we must find a new area of memory to use that
        // is different than the specified one. Super Mario Odyssey hits this scenario when changing
        // areas, but we do not want to overwrite the old pages.
        // TODO(bunnei): We need to write a hardware test to confirm this behavior.

        LOG_ERROR(HW_GPU, "attempting to map addr 0x{:016X}, which is not available!", gpu_addr);

        const std::optional<GPUVAddr> new_gpu_addr{
            FindFreeBlock(gpu_addr, size, PAGE_SIZE, PageStatus::Allocated)};

        ASSERT_MSG(new_gpu_addr, "unable to find available GPU memory");

        gpu_addr = *new_gpu_addr;
    }

    for (u64 offset{}; offset < size; offset += PAGE_SIZE) {
        VAddr& slot{PageSlot(gpu_addr + offset)};

        ASSERT(slot == static_cast<u64>(PageStatus::Allocated));

        slot = cpu_addr + offset;
    }

    const MappedRegion region{cpu_addr, gpu_addr, size};
    mapped_regions.push_back(region);

    return gpu_addr;
}

GPUVAddr MemoryManager::UnmapBuffer(GPUVAddr gpu_addr, u64 size) {
    ASSERT((gpu_addr & PAGE_MASK) == 0);

    for (u64 offset{}; offset < size; offset += PAGE_SIZE) {
        VAddr& slot{PageSlot(gpu_addr + offset)};

        ASSERT(slot != static_cast<u64>(PageStatus::Allocated) &&
               slot != static_cast<u64>(PageStatus::Unmapped));

        slot = static_cast<u64>(PageStatus::Unmapped);
    }

    // Delete the region mappings that are contained within the unmapped region
    mapped_regions.erase(std::remove_if(mapped_regions.begin(), mapped_regions.end(),
                                        [&](const MappedRegion& region) {
                                            return region.gpu_addr <= gpu_addr &&
                                                   region.gpu_addr + region.size < gpu_addr + size;
                                        }),
                         mapped_regions.end());
    return gpu_addr;
}

GPUVAddr MemoryManager::GetRegionEnd(GPUVAddr region_start) const {
    for (const auto& region : mapped_regions) {
        const GPUVAddr region_end{region.gpu_addr + region.size};
        if (region_start >= region.gpu_addr && region_start < region_end) {
            return region_end;
        }
    }
    return {};
}

std::optional<GPUVAddr> MemoryManager::FindFreeBlock(GPUVAddr region_start, u64 size, u64 align,
                                                     PageStatus status) {
    GPUVAddr gpu_addr{region_start};
    u64 free_space{};
    align = (align + PAGE_MASK) & ~PAGE_MASK;

    while (gpu_addr + free_space < MAX_ADDRESS) {
        if (PageSlot(gpu_addr + free_space) == static_cast<u64>(status)) {
            free_space += PAGE_SIZE;
            if (free_space >= size) {
                return gpu_addr;
            }
        } else {
            gpu_addr += free_space + PAGE_SIZE;
            free_space = 0;
            gpu_addr = Common::AlignUp(gpu_addr, align);
        }
    }

    return {};
}

std::optional<VAddr> MemoryManager::GpuToCpuAddress(GPUVAddr gpu_addr) {
    const VAddr base_addr{PageSlot(gpu_addr)};

    if (base_addr == static_cast<u64>(PageStatus::Allocated) ||
        base_addr == static_cast<u64>(PageStatus::Unmapped) ||
        base_addr == static_cast<u64>(PageStatus::Reserved)) {
        return {};
    }

    return base_addr + (gpu_addr & PAGE_MASK);
}

u8 MemoryManager::Read8(GPUVAddr addr) {
    return Memory::Read8(*GpuToCpuAddress(addr));
}

u16 MemoryManager::Read16(GPUVAddr addr) {
    return Memory::Read16(*GpuToCpuAddress(addr));
}

u32 MemoryManager::Read32(GPUVAddr addr) {
    return Memory::Read32(*GpuToCpuAddress(addr));
}

u64 MemoryManager::Read64(GPUVAddr addr) {
    return Memory::Read64(*GpuToCpuAddress(addr));
}

void MemoryManager::Write8(GPUVAddr addr, u8 data) {
    Memory::Write8(*GpuToCpuAddress(addr), data);
}

void MemoryManager::Write16(GPUVAddr addr, u16 data) {
    Memory::Write16(*GpuToCpuAddress(addr), data);
}

void MemoryManager::Write32(GPUVAddr addr, u32 data) {
    Memory::Write32(*GpuToCpuAddress(addr), data);
}

void MemoryManager::Write64(GPUVAddr addr, u64 data) {
    Memory::Write64(*GpuToCpuAddress(addr), data);
}

u8* MemoryManager::GetPointer(GPUVAddr addr) {
    return Memory::GetPointer(*GpuToCpuAddress(addr));
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

VAddr& MemoryManager::PageSlot(GPUVAddr gpu_addr) {
    auto& block{page_table[(gpu_addr >> (PAGE_BITS + PAGE_TABLE_BITS)) & PAGE_TABLE_MASK]};
    if (!block) {
        block = std::make_unique<PageBlock>();
        block->fill(static_cast<VAddr>(PageStatus::Unmapped));
    }
    return (*block)[(gpu_addr >> PAGE_BITS) & PAGE_BLOCK_MASK];
}

} // namespace Tegra
