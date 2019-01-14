// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "video_core/dma_pusher.h"

namespace Tegra {
struct FramebufferConfig;
}

namespace VideoCore {

class RendererBase;

struct GPUThreadState final {
    bool is_running{true};
    bool is_dma_pending{};
    bool is_swapbuffers_pending{};
    std::optional<Tegra::FramebufferConfig> pending_swapbuffers_config;
    std::condition_variable signal_condition;
    std::condition_variable running_condition;
    std::mutex signal_mutex;
    std::mutex running_mutex;
    std::recursive_mutex cache_mutex;

    struct MemoryRegion final {
        const VAddr addr;
        const u64 size;
    };

    std::vector<MemoryRegion> flush_regions;
    std::vector<MemoryRegion> invalidate_regions;
};

class GPUThread final {
public:
    explicit GPUThread(RendererBase& renderer, Tegra::DmaPusher& dma_pusher);
    ~GPUThread();

    /// Push GPU command entries to be processed
    void PushGPUEntries(Tegra::CommandList&& entries);

    /// Swap buffers (render frame)
    void SwapBuffers(
        std::optional<std::reference_wrapper<const Tegra::FramebufferConfig>> framebuffer);

    /// Notify rasterizer that any caches of the specified region should be flushed to Switch memory
    void FlushRegion(VAddr addr, u64 size);

    /// Notify rasterizer that any caches of the specified region should be invalidated
    void InvalidateRegion(VAddr addr, u64 size);

private:
    GPUThreadState state;
    std::unique_ptr<std::thread> thread;
    Tegra::DmaPusher& dma_pusher;
};

} // namespace VideoCore
