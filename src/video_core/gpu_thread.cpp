// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/frontend/scope_acquire_window_context.h"
#include "video_core/gpu.h"
#include "video_core/gpu_thread.h"
#include "video_core/renderer_base.h"

namespace {
static void RunThread(VideoCore::RendererBase& renderer, Tegra::DmaPusher& dma_pusher,
                      VideoCore::GPUThreadState& state) {

    Core::Frontend::ScopeAcquireWindowContext acquire_context{renderer.GetRenderWindow()};

    while (state.is_running) {
        bool is_dma_pending{};
        bool is_swapbuffers_pending{};

        {
            // Wait for CPU thread to send GPU commands
            std::unique_lock<std::mutex> lock{state.signal_mutex};
            state.signal_condition.wait(lock, [&] {
                return state.is_dma_pending || state.is_swapbuffers_pending || !state.is_running;
            });

            if (!state.is_running) {
                return;
            }

            is_dma_pending = state.is_dma_pending;
            is_swapbuffers_pending = state.is_swapbuffers_pending;

            if (is_dma_pending) {
                dma_pusher.QueuePendingCalls();
                state.is_dma_pending = false;
            }
        }

        if (is_dma_pending) {
            // Process pending DMA pushbuffer commands
            std::lock_guard<std::mutex> lock{state.running_mutex};
            dma_pusher.DispatchCalls();
        }

        {
            // Cache management
            std::lock_guard<std::recursive_mutex> lock{state.cache_mutex};

            for (const auto& region : state.flush_regions) {
                renderer.Rasterizer().FlushRegion(region.addr, region.size);
            }

            for (const auto& region : state.invalidate_regions) {
                renderer.Rasterizer().InvalidateRegion(region.addr, region.size);
            }

            state.flush_regions.clear();
            state.invalidate_regions.clear();
        }

        if (is_swapbuffers_pending) {
            // Process pending SwapBuffers
            renderer.SwapBuffers(state.pending_swapbuffers_config);
            state.is_swapbuffers_pending = false;
            state.signal_condition.notify_one();
        }
    }
}
} // Anonymous namespace

namespace VideoCore {

GPUThread::GPUThread(RendererBase& renderer, Tegra::DmaPusher& dma_pusher)
    : dma_pusher{dma_pusher} {
    thread = std::make_unique<std::thread>(RunThread, std::ref(renderer), std::ref(dma_pusher),
                                           std::ref(state));
}

GPUThread::~GPUThread() {
    {
        // Notify GPU thread that a shutdown is pending
        std::lock_guard<std::mutex> lock{state.signal_mutex};
        state.is_running = false;
    }

    state.signal_condition.notify_one();
    thread->join();
}

void GPUThread::PushGPUEntries(Tegra::CommandList&& entries) {
    if (entries.empty()) {
        return;
    }

    {
        // Notify GPU thread that data is available
        std::lock_guard<std::mutex> lock{state.signal_mutex};
        dma_pusher.Push(std::move(entries));
        state.is_dma_pending = true;
    }

    state.signal_condition.notify_one();
}

void GPUThread::SwapBuffers(
    std::optional<std::reference_wrapper<const Tegra::FramebufferConfig>> framebuffer) {

    {
        // Notify GPU thread that we should SwapBuffers
        std::lock_guard<std::mutex> lock{state.signal_mutex};
        state.pending_swapbuffers_config = framebuffer;
        state.is_swapbuffers_pending = true;
    }

    state.signal_condition.notify_one();

    {
        // Wait for SwapBuffers
        std::unique_lock<std::mutex> lock{state.signal_mutex};
        state.signal_condition.wait(lock, [this] { return !state.is_swapbuffers_pending; });
    }
}

void GPUThread::FlushRegion(VAddr addr, u64 size) {
    std::lock_guard<std::recursive_mutex> lock{state.cache_mutex};
    state.flush_regions.push_back({addr, size});
}

void GPUThread::InvalidateRegion(VAddr addr, u64 size) {
    std::lock_guard<std::recursive_mutex> lock{state.cache_mutex};
    state.invalidate_regions.push_back({addr, size});
}

} // namespace VideoCore
