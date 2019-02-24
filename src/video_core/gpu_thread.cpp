// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/microprofile.h"
#include "core/frontend/scope_acquire_window_context.h"
#include "core/settings.h"
#include "video_core/dma_pusher.h"
#include "video_core/gpu.h"
#include "video_core/gpu_thread.h"
#include "video_core/renderer_base.h"

namespace VideoCommon::GPUThread {

/// Executes a single GPU thread command
static void ExecuteCommand(CommandData* command, VideoCore::RendererBase& renderer,
                           Tegra::DmaPusher& dma_pusher) {
    if (const auto submit_list = std::get_if<SubmitListCommand>(command)) {
        dma_pusher.Push(std::move(submit_list->entries));
        dma_pusher.DispatchCalls();
    } else if (const auto data = std::get_if<SwapBuffersCommand>(command)) {
        renderer.SwapBuffers(data->framebuffer);
    } else if (const auto data = std::get_if<FlushRegionCommand>(command)) {
        renderer.Rasterizer().FlushRegion(data->addr, data->size);
    } else if (const auto data = std::get_if<InvalidateRegionCommand>(command)) {
        renderer.Rasterizer().InvalidateRegion(data->addr, data->size);
    } else if (const auto data = std::get_if<FlushAndInvalidateRegionCommand>(command)) {
        renderer.Rasterizer().FlushAndInvalidateRegion(data->addr, data->size);
    } else {
        UNREACHABLE();
    }
}

/// Runs the GPU thread
static void RunThread(VideoCore::RendererBase& renderer, Tegra::DmaPusher& dma_pusher,
                      SynchState& state) {

    MicroProfileOnThreadCreate("GpuThread");

    auto WaitForWakeup = [&]() {
        std::unique_lock<std::mutex> lock{state.signal_mutex};
        state.signal_condition.wait(lock, [&] { return !state.is_idle || !state.is_running; });
    };

    // Wait for first GPU command before acquiring the window context
    WaitForWakeup();

    // If emulation was stopped during disk shader loading, abort before trying to acquire context
    if (!state.is_running) {
        return;
    }

    Core::Frontend::ScopeAcquireWindowContext acquire_context{renderer.GetRenderWindow()};

    while (state.is_running) {
        if (!state.is_running) {
            return;
        }

        {
            // Thread has been woken up, so make the previous write queue the next read queue
            std::lock_guard<std::mutex> lock{state.signal_mutex};
            std::swap(state.push_queue, state.pop_queue);
        }

        // Execute all of the GPU commands
        while (!state.pop_queue->empty()) {
            ExecuteCommand(&state.pop_queue->front(), renderer, dma_pusher);
            state.pop_queue->pop();
        }

        state.UpdateIdleState();

        // Signal that the GPU thread has finished processing commands
        if (state.is_idle) {
            state.idle_condition.notify_one();
        }

        // Wait for CPU thread to send more GPU commands
        WaitForWakeup();
    }
}

ThreadManager::ThreadManager(VideoCore::RendererBase& renderer, Tegra::DmaPusher& dma_pusher)
    : renderer{renderer}, dma_pusher{dma_pusher}, thread{RunThread, std::ref(renderer),
                                                         std::ref(dma_pusher), std::ref(state)},
      thread_id{thread.get_id()} {}

ThreadManager::~ThreadManager() {
    {
        // Notify GPU thread that a shutdown is pending
        std::lock_guard<std::mutex> lock{state.signal_mutex};
        state.is_running = false;
    }

    state.signal_condition.notify_one();
    thread.join();
}

void ThreadManager::SubmitList(Tegra::CommandList&& entries) {
    if (entries.empty()) {
        return;
    }

    PushCommand(SubmitListCommand(std::move(entries)), false, false);
}

void ThreadManager::SwapBuffers(
    std::optional<std::reference_wrapper<const Tegra::FramebufferConfig>> framebuffer) {
    PushCommand(SwapBuffersCommand(std::move(framebuffer)), true, false);
}

void ThreadManager::FlushRegion(VAddr addr, u64 size) {
    // Block the CPU when using accurate emulation
    PushCommand(FlushRegionCommand(addr, size), Settings::values.use_accurate_gpu_emulation, false);
}

void ThreadManager::InvalidateRegion(VAddr addr, u64 size) {
    PushCommand(InvalidateRegionCommand(addr, size), true, true);
}

void ThreadManager::FlushAndInvalidateRegion(VAddr addr, u64 size) {
    InvalidateRegion(addr, size);
}

void ThreadManager::PushCommand(CommandData&& command_data, bool wait_for_idle, bool allow_on_cpu) {
    {
        std::lock_guard<std::mutex> lock{state.signal_mutex};

        if ((allow_on_cpu && state.is_idle) || IsGpuThread()) {
            // Execute the command synchronously on the current thread
            ExecuteCommand(&command_data, renderer, dma_pusher);
            return;
        }

        // Push the command to the GPU thread
        state.UpdateIdleState();
        state.push_queue->emplace(command_data);
    }

    // Signal the GPU thread that commands are pending
    state.signal_condition.notify_one();

    if (wait_for_idle) {
        // Wait for the GPU to be idle (all commands to be executed)
        std::unique_lock<std::mutex> lock{state.idle_mutex};
        state.idle_condition.wait(lock, [this] { return static_cast<bool>(state.is_idle); });
    }
}

} // namespace VideoCommon::GPUThread
