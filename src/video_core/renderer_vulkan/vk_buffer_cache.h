// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <tuple>

#include "common/common_types.h"
#include "video_core/gpu.h"
#include "video_core/rasterizer_cache.h"
#include "video_core/renderer_vulkan/declarations.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Tegra {
class MemoryManager;
}

namespace Vulkan {

class VKDevice;
class VKFence;
class VKMemoryManager;
class VKStreamBuffer;

class CachedBufferEntry final : public RasterizerCacheObject {
public:
    explicit CachedBufferEntry(VAddr cpu_addr, std::size_t size, u64 offset, std::size_t alignment,
                               u8* host_ptr);

    VAddr GetCpuAddr() const override {
        return cpu_addr;
    }

    std::size_t GetSizeInBytes() const override {
        return size;
    }

    std::size_t GetSize() const {
        return size;
    }

    u64 GetOffset() const {
        return offset;
    }

    std::size_t GetAlignment() const {
        return alignment;
    }

    // We do not have to flush this cache as things in it are never modified by us.
    void Flush() override {}

private:
    VAddr cpu_addr{};
    std::size_t size{};
    u64 offset{};
    std::size_t alignment{};
};

class VKBufferCache final : public RasterizerCache<std::shared_ptr<CachedBufferEntry>> {
public:
    explicit VKBufferCache(Tegra::MemoryManager& tegra_memory_manager,
                           VideoCore::RasterizerInterface& rasterizer, const VKDevice& device,
                           VKMemoryManager& memory_manager, VKScheduler& scheduler, u64 size);
    ~VKBufferCache();

    /// Uploads data from a guest GPU address. Returns host's buffer offset where it's been
    /// allocated.
    u64 UploadMemory(GPUVAddr gpu_addr, std::size_t size, u64 alignment = 4, bool cache = true);

    /// Uploads from a host memory. Returns host's buffer offset where it's been allocated.
    u64 UploadHostMemory(const u8* raw_pointer, std::size_t size, u64 alignment = 4);

    /// Reserves memory to be used by host's CPU. Returns mapped address and offset.
    std::tuple<u8*, u64> ReserveMemory(std::size_t size, u64 alignment = 4);

    /// Reserves a region of memory to be used in subsequent upload/reserve operations.
    void Reserve(std::size_t max_size);

    /// Ensures that the set data is sent to the device.
    [[nodiscard]] VKExecutionContext Send(VKExecutionContext exctx);

    /// Returns the buffer cache handle.
    vk::Buffer GetBuffer() const {
        return buffer_handle;
    }

private:
    void AlignBuffer(std::size_t alignment);

    Tegra::MemoryManager& tegra_memory_manager;

    std::unique_ptr<VKStreamBuffer> stream_buffer;
    vk::Buffer buffer_handle;

    u8* buffer_ptr = nullptr;
    u64 buffer_offset = 0;
    u64 buffer_offset_base = 0;
};

} // namespace Vulkan
