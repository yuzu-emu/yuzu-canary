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
    while (state.queue.Empty())
        ;

    // If emulation was stopped during disk shader loading, abort before trying to acquire context
    if (!state.is_running) {
        return;
    }

    Core::Frontend::ScopeAcquireWindowContext acquire_context{renderer.GetRenderWindow()};

    CommandDataContainer next;
    while (state.is_running) {
        while (!state.queue.Empty()) {
            state.queue.Pop(next);
            if (const auto submit_list = std::get_if<SubmitListCommand>(&next.data)) {
                dma_pusher.Push(std::move(submit_list->entries));
                dma_pusher.DispatchCalls();
            } else if (const auto data = std::get_if<SwapBuffersCommand>(&next.data)) {
                renderer.SwapBuffers(data->framebuffer ? &*data->framebuffer : nullptr);
            } else if (const auto data = std::get_if<FlushRegionCommand>(&next.data)) {
                renderer.Rasterizer().FlushRegion(data->addr, data->size);
            } else if (const auto data = std::get_if<InvalidateRegionCommand>(&next.data)) {
                renderer.Rasterizer().InvalidateRegion(data->addr, data->size);
            } else if (std::holds_alternative<EndProcessingCommand>(next.data)) {
                return;
            } else {
                UNREACHABLE();
            }
            state.signaled_fence.store(next.fence);
        }
    }
}

ThreadManager::ThreadManager(Core::System& system) : system{system} {}

ThreadManager::~ThreadManager() {
    if (!thread.joinable()) {
        return;
    }

    // Notify GPU thread that a shutdown is pending
    PushCommand(EndProcessingCommand());
    thread.join();
}

void ThreadManager::StartThread(VideoCore::RendererBase& renderer, Tegra::DmaPusher& dma_pusher) {
    thread = std::thread{RunThread, std::ref(renderer), std::ref(dma_pusher), std::ref(state)};
    synchronization_event = system.CoreTiming().RegisterEvent(
        "GPUThreadSynch", [this](u64 fence, s64) { state.WaitForSynchronization(fence); });
}

void ThreadManager::SubmitList(Tegra::CommandList&& entries) {
    const u64 fence{PushCommand(SubmitListCommand(std::move(entries)))};
    const s64 synchronization_ticks{Core::Timing::usToCycles(std::chrono::microseconds{9000})};
    system.CoreTiming().ScheduleEvent(synchronization_ticks, synchronization_event, fence);
}

void ThreadManager::SwapBuffers(const Tegra::FramebufferConfig* framebuffer) {
    PushCommand(SwapBuffersCommand(framebuffer ? *framebuffer
                                               : std::optional<const Tegra::FramebufferConfig>{}));
}

void ThreadManager::FlushRegion(CacheAddr addr, u64 size) {
    PushCommand(FlushRegionCommand(addr, size));
}

void ThreadManager::InvalidateRegion(CacheAddr addr, u64 size) {
    system.Renderer().Rasterizer().InvalidateRegion(addr, size);
}

void ThreadManager::FlushAndInvalidateRegion(CacheAddr addr, u64 size) {
    // Skip flush on asynch mode, as FlushAndInvalidateRegion is not used for anything too important
    InvalidateRegion(addr, size);
}

u64 ThreadManager::PushCommand(CommandData&& command_data) {
    const u64 fence{++state.last_fence};
    state.queue.Push(CommandDataContainer(std::move(command_data), fence));
    return fence;
}

MICROPROFILE_DEFINE(GPU_wait, "GPU", "Wait for the GPU", MP_RGB(128, 128, 192));
void SynchState::WaitForSynchronization(u64 fence) {
    while (signaled_fence.load() < fence)
        ;
}

} // namespace VideoCommon::GPUThread
