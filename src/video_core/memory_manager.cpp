// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/alignment.h"
#include "common/assert.h"
#include "common/logging/log.h"
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
        base_addr == static_cast<u64>(PageStatus::Unmapped)) {
        return {};
    }

    return base_addr + (gpu_addr & PAGE_MASK);
}

std::vector<GPUVAddr> MemoryManager::CpuToGpuAddress(VAddr cpu_addr) const {
    std::vector<GPUVAddr> results;
    for (const auto& region : mapped_regions) {
        if (cpu_addr >= region.cpu_addr && cpu_addr < (region.cpu_addr + region.size)) {
            const u64 offset{cpu_addr - region.cpu_addr};
            results.push_back(region.gpu_addr + offset);
        }
    }
    return results;
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
