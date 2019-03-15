// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/microprofile.h"
#include "core/frontend/scope_acquire_window_context.h"
#include "video_core/dma_pusher.h"
#include "video_core/gpu.h"
#include "video_core/gpu_thread.h"
#include "video_core/renderer_base.h"

namespace VideoCommon::GPUThread {

/// Runs the GPU thread
static void RunThread(VideoCore::RendererBase& renderer, Tegra::DmaPusher& dma_pusher,
                      SynchState& state) {
    MicroProfileOnThreadCreate("GpuThread");

    // Wait for first GPU command before acquiring the window context
    state.WaitForCommands();

    // If emulation was stopped during disk shader loading, abort before trying to acquire context
    if (!state.is_running) {
        return;
    }

    Core::Frontend::ScopeAcquireWindowContext acquire_context{renderer.GetRenderWindow()};

    CommandDataContainer next;
    while (state.is_running) {
        state.WaitForCommands();
        while (!state.queue.Empty()) {
            state.queue.Pop(next);
            if (const auto submit_list = std::get_if<SubmitListCommand>(&next.data)) {
                dma_pusher.Push(std::move(submit_list->entries));
                dma_pusher.DispatchCalls();
            } else if (const auto data = std::get_if<SwapBuffersCommand>(&next.data)) {
                state.DecrementFramesCounter();
                renderer.SwapBuffers(std::move(data->framebuffer));
            } else if (const auto data = std::get_if<FlushRegionCommand>(&next.data)) {
                renderer.Rasterizer().FlushRegion(data->addr, data->size);
            } else if (const auto data = std::get_if<InvalidateRegionCommand>(&next.data)) {
                renderer.Rasterizer().InvalidateRegion(data->addr, data->size);
            } else if (const auto data = std::get_if<EndProcessingCommand>(&next.data)) {
                return;
            } else {
                UNREACHABLE();
            }
        }
    }
}

ThreadManager::ThreadManager(VideoCore::RendererBase& renderer, Tegra::DmaPusher& dma_pusher)
    : renderer{renderer}, dma_pusher{dma_pusher}, thread{RunThread, std::ref(renderer),
                                                         std::ref(dma_pusher), std::ref(state)} {}

ThreadManager::~ThreadManager() {
    // Notify GPU thread that a shutdown is pending
    PushCommand(EndProcessingCommand());
    thread.join();
}

void ThreadManager::SubmitList(Tegra::CommandList&& entries) {
    PushCommand(SubmitListCommand(std::move(entries)));
}

void ThreadManager::SwapBuffers(
    std::optional<std::reference_wrapper<const Tegra::FramebufferConfig>> framebuffer) {
    state.IncrementFramesCounter();
    PushCommand(SwapBuffersCommand(std::move(framebuffer)));
    state.WaitForFrames();
}

void ThreadManager::FlushRegion(CacheAddr addr, u64 size) {
    PushCommand(FlushRegionCommand(addr, size));
}

void ThreadManager::InvalidateRegion(CacheAddr addr, u64 size) {
    if (state.queue.Empty()) {
        // It's quicker to invalidate a single region on the CPU if the queue is already empty
        renderer.Rasterizer().InvalidateRegion(addr, size);
    } else {
        PushCommand(InvalidateRegionCommand(addr, size));
    }
}

void ThreadManager::FlushAndInvalidateRegion(CacheAddr addr, u64 size) {
    // Skip flush on asynch mode, as FlushAndInvalidateRegion is not used for anything too important
    InvalidateRegion(addr, size);
}

void ThreadManager::PushCommand(CommandData&& command_data) {
    state.queue.Push(CommandDataContainer(std::move(command_data)));
    state.SignalCommands();
}

} // namespace VideoCommon::GPUThread
