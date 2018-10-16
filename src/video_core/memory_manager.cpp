// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/alignment.h"
#include "common/assert.h"
#include "video_core/memory_manager.h"

namespace Tegra {

GPUVAddr MemoryManager::AllocateSpace(u64 size, u64 align) {
    boost::optional<GPUVAddr> gpu_addr = FindFreeBlock(size, align);
    ASSERT(gpu_addr);

    for (u64 offset = 0; offset < size; offset += PAGE_SIZE) {
        VAddr& slot = PageSlot(*gpu_addr + offset);

        ASSERT(slot == static_cast<u64>(PageStatus::Unmapped));
        slot = static_cast<u64>(PageStatus::Allocated);
    }

    return *gpu_addr;
}

GPUVAddr MemoryManager::AllocateSpace(GPUVAddr gpu_addr, u64 size, u64 align) {
    for (u64 offset = 0; offset < size; offset += PAGE_SIZE) {
        VAddr& slot = PageSlot(gpu_addr + offset);

        ASSERT(slot == static_cast<u64>(PageStatus::Unmapped));
        slot = static_cast<u64>(PageStatus::Allocated);
    }

    return gpu_addr;
}

GPUVAddr MemoryManager::MapBufferEx(VAddr cpu_addr, u64 size) {
    boost::optional<GPUVAddr> gpu_addr = FindFreeBlock(size, PAGE_SIZE);
    ASSERT(gpu_addr);

    for (u64 offset = 0; offset < size; offset += PAGE_SIZE) {
        VAddr& slot = PageSlot(*gpu_addr + offset);

        ASSERT(slot == static_cast<u64>(PageStatus::Unmapped));
        slot = cpu_addr + offset;
    }

    MappedRegion region{cpu_addr, *gpu_addr, size};
    mapped_regions.push_back(region);

    return *gpu_addr;
}

GPUVAddr MemoryManager::MapBufferEx(VAddr cpu_addr, GPUVAddr gpu_addr, u64 size) {
    ASSERT((gpu_addr & PAGE_MASK) == 0);

    for (u64 offset = 0; offset < size; offset += PAGE_SIZE) {
        VAddr& slot = PageSlot(gpu_addr + offset);

        ASSERT(slot == static_cast<u64>(PageStatus::Allocated));
        slot = cpu_addr + offset;
    }

    MappedRegion region{cpu_addr, gpu_addr, size};
    mapped_regions.push_back(region);

    return gpu_addr;
}

GPUVAddr MemoryManager::UnmapBuffer(GPUVAddr gpu_addr, u64 size) {
    ASSERT((gpu_addr & PAGE_MASK) == 0);

    for (u64 offset = 0; offset < size; offset += PAGE_SIZE) {
        VAddr& slot = PageSlot(gpu_addr + offset);

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

boost::optional<GPUVAddr> MemoryManager::FindFreeBlock(u64 size, u64 align) {
    GPUVAddr gpu_addr = 0;
    u64 free_space = 0;
    align = (align + PAGE_MASK) & ~PAGE_MASK;

    while (gpu_addr + free_space < MAX_ADDRESS) {
        if (!IsPageMapped(gpu_addr + free_space)) {
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

boost::optional<VAddr> MemoryManager::GpuToCpuAddress(GPUVAddr gpu_addr) {
    VAddr base_addr = PageSlot(gpu_addr);

    if (base_addr == static_cast<u64>(PageStatus::Allocated) ||
        base_addr == static_cast<u64>(PageStatus::Unmapped)) {
        return {};
    }

    return base_addr + (gpu_addr & PAGE_MASK);
}

std::vector<GPUVAddr> MemoryManager::CpuToGpuAddress(VAddr cpu_addr) const {
    std::vector<GPUVAddr> results;
    for (const auto& region : mapped_regions) {
        if (cpu_addr >= region.cpu_addr && cpu_addr < (region.cpu_addr + region.size)) {
            u64 offset = cpu_addr - region.cpu_addr;
            results.push_back(region.gpu_addr + offset);
        }
    }
    return results;
}

bool MemoryManager::IsPageMapped(GPUVAddr gpu_addr) {
    return PageSlot(gpu_addr) != static_cast<u64>(PageStatus::Unmapped);
}

VAddr& MemoryManager::PageSlot(GPUVAddr gpu_addr) {
    auto& block = page_table[(gpu_addr >> (PAGE_BITS + PAGE_TABLE_BITS)) & PAGE_TABLE_MASK];
    if (!block) {
        block = std::make_unique<PageBlock>();
        block->fill(static_cast<VAddr>(PageStatus::Unmapped));
    }
    return (*block)[(gpu_addr >> PAGE_BITS) & PAGE_BLOCK_MASK];
}

} // namespace Tegra
