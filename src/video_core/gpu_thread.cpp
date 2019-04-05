// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/microprofile.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/core_timing_util.h"
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
            state.signaled_fence = next.fence;
            state.TrySynchronize();
        }
    }
}

ThreadManager::ThreadManager(Core::System& system, VideoCore::RendererBase& renderer,
                             Tegra::DmaPusher& dma_pusher)
    : system{system}, thread{RunThread, std::ref(renderer), std::ref(dma_pusher), std::ref(state)} {
    synchronization_event = system.CoreTiming().RegisterEvent(
        "GPUThreadSynch", [this](u64 fence, int) { state.WaitForSynchronization(fence); });
}

ThreadManager::~ThreadManager() {
    // Notify GPU thread that a shutdown is pending
    PushCommand(EndProcessingCommand());
    thread.join();
}

void ThreadManager::SubmitList(Tegra::CommandList&& entries) {
    const u64 fence{PushCommand(SubmitListCommand(std::move(entries)))};
    const s64 synchronization_ticks{Core::Timing::usToCycles(9000)};
    system.CoreTiming().ScheduleEvent(synchronization_ticks, synchronization_event, fence);
}

void ThreadManager::SwapBuffers(
    std::optional<std::reference_wrapper<const Tegra::FramebufferConfig>> framebuffer) {
    PushCommand(SwapBuffersCommand(std::move(framebuffer)));
}

void ThreadManager::FlushRegion(CacheAddr addr, u64 size) {
    PushCommand(FlushRegionCommand(addr, size));
}

void ThreadManager::InvalidateRegion(CacheAddr addr, u64 size) {
    if (state.queue.Empty()) {
        // It's quicker to invalidate a single region on the CPU if the queue is already empty
        system.Renderer().Rasterizer().InvalidateRegion(addr, size);
    } else {
        PushCommand(InvalidateRegionCommand(addr, size));
    }
}

void ThreadManager::FlushAndInvalidateRegion(CacheAddr addr, u64 size) {
    // Skip flush on asynch mode, as FlushAndInvalidateRegion is not used for anything too important
    InvalidateRegion(addr, size);
}

u64 ThreadManager::PushCommand(CommandData&& command_data) {
    const u64 fence{++state.last_fence};
    state.queue.Push(CommandDataContainer(std::move(command_data), fence));
    state.SignalCommands();
    return fence;
}

MICROPROFILE_DEFINE(GPU_wait, "GPU", "Wait for the GPU", MP_RGB(128, 128, 192));
void SynchState::WaitForSynchronization(u64 fence) {
    if (signaled_fence >= fence) {
        return;
    }

    // Wait for the GPU to be idle (all commands to be executed)
    {
        MICROPROFILE_SCOPE(GPU_wait);
        std::unique_lock<std::mutex> lock{synchronization_mutex};
        synchronization_condition.wait(lock, [this, fence] { return signaled_fence >= fence; });
    }
}

} // namespace VideoCommon::GPUThread
