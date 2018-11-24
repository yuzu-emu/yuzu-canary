// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <memory>
#include <optional>
#include <vector>

#include "common/common_types.h"

namespace Tegra {

/// Virtual addresses in the GPU's memory map are 64 bit.
using GPUVAddr = u64;

class MemoryManager final {
public:
    MemoryManager();

    GPUVAddr AllocateSpace(u64 size, u64 align);
    GPUVAddr AllocateSpace(GPUVAddr gpu_addr, u64 size, u64 align);
    GPUVAddr MapBufferEx(VAddr cpu_addr, u64 size);
    GPUVAddr MapBufferEx(VAddr cpu_addr, GPUVAddr gpu_addr, u64 size);
    GPUVAddr UnmapBuffer(GPUVAddr gpu_addr, u64 size);
    GPUVAddr GetRegionEnd(GPUVAddr region_start) const;
    std::optional<VAddr> GpuToCpuAddress(GPUVAddr gpu_addr);
    std::vector<GPUVAddr> CpuToGpuAddress(VAddr cpu_addr) const;

    static constexpr u64 PAGE_BITS = 16;
    static constexpr u64 PAGE_SIZE = 1 << PAGE_BITS;
    static constexpr u64 PAGE_MASK = PAGE_SIZE - 1;

private:
    enum class PageStatus : u64 {
        Unmapped = 0xFFFFFFFFFFFFFFFFULL,
        Allocated = 0xFFFFFFFFFFFFFFFEULL,
        Reserved = 0xFFFFFFFFFFFFFFFDULL,
    };

    std::optional<GPUVAddr> FindFreeBlock(GPUVAddr region_start, u64 size, u64 align,
                                          PageStatus status);
    VAddr& PageSlot(GPUVAddr gpu_addr);

    static constexpr u64 MAX_ADDRESS{0x10000000000ULL};
    static constexpr u64 PAGE_TABLE_BITS{10};
    static constexpr u64 PAGE_TABLE_SIZE{1 << PAGE_TABLE_BITS};
    static constexpr u64 PAGE_TABLE_MASK{PAGE_TABLE_SIZE - 1};
    static constexpr u64 PAGE_BLOCK_BITS{14};
    static constexpr u64 PAGE_BLOCK_SIZE{1 << PAGE_BLOCK_BITS};
    static constexpr u64 PAGE_BLOCK_MASK{PAGE_BLOCK_SIZE - 1};

    using PageBlock = std::array<VAddr, PAGE_BLOCK_SIZE>;
    std::array<std::unique_ptr<PageBlock>, PAGE_TABLE_SIZE> page_table{};

    struct MappedRegion {
        VAddr cpu_addr;
        GPUVAddr gpu_addr;
        u64 size;
    };

    std::vector<MappedRegion> mapped_regions;
};

} // namespace Tegra
