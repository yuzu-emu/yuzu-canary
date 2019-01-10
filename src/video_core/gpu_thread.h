// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <condition_variable>
#include <functional>
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
    std::recursive_mutex running_mutex;
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

    /// Waits the caller until the thread is idle, and then calls the callback
    void WaitUntilIdle(std::function<void()> callback);

private:
    GPUThreadState state;
    std::unique_ptr<std::thread> thread;
    Tegra::DmaPusher& dma_pusher;
};

} // namespace VideoCore
