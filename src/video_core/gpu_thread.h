// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <variant>

#include "common/threadsafe_queue.h"
#include "video_core/gpu.h"

namespace Tegra {
struct FramebufferConfig;
class DmaPusher;
} // namespace Tegra

namespace VideoCore {
class RendererBase;
} // namespace VideoCore

namespace VideoCommon::GPUThread {

/// Command to signal to the GPU thread that processing has ended
struct EndProcessingCommand final {};

/// Command to signal to the GPU thread that a command list is ready for processing
struct SubmitListCommand final {
    explicit SubmitListCommand(Tegra::CommandList&& entries) : entries{std::move(entries)} {}

    Tegra::CommandList entries;
};

/// Command to signal to the GPU thread that a swap buffers is pending
struct SwapBuffersCommand final {
    explicit SwapBuffersCommand(std::optional<const Tegra::FramebufferConfig> framebuffer)
        : framebuffer{std::move(framebuffer)} {}

    std::optional<Tegra::FramebufferConfig> framebuffer;
};

/// Command to signal to the GPU thread to flush a region
struct FlushRegionCommand final {
    explicit constexpr FlushRegionCommand(CacheAddr addr, u64 size) : addr{addr}, size{size} {}

    CacheAddr addr;
    u64 size;
};

/// Command to signal to the GPU thread to invalidate a region
struct InvalidateRegionCommand final {
    explicit constexpr InvalidateRegionCommand(CacheAddr addr, u64 size) : addr{addr}, size{size} {}

    CacheAddr addr;
    u64 size;
};

/// Command to signal to the GPU thread to flush and invalidate a region
struct FlushAndInvalidateRegionCommand final {
    explicit constexpr FlushAndInvalidateRegionCommand(CacheAddr addr, u64 size)
        : addr{addr}, size{size} {}

    CacheAddr addr;
    u64 size;
};

using CommandData =
    std::variant<EndProcessingCommand, SubmitListCommand, SwapBuffersCommand, FlushRegionCommand,
                 InvalidateRegionCommand, FlushAndInvalidateRegionCommand>;

struct CommandDataContainer {
    CommandDataContainer() = default;

    CommandDataContainer(CommandData&& data) : data{std::move(data)} {}

    CommandDataContainer& operator=(const CommandDataContainer& t) {
        data = std::move(t.data);
        return *this;
    }

    CommandData data;
};

/// Struct used to synchronize the GPU thread
struct SynchState final {
    std::atomic_bool is_running{true};
    std::atomic_int queued_frame_count{};
    std::mutex frames_mutex;
    std::mutex commands_mutex;
    std::condition_variable commands_condition;
    std::condition_variable frames_condition;

    void IncrementFramesCounter() {
        std::lock_guard<std::mutex> lock{frames_mutex};
        ++queued_frame_count;
    }

    void DecrementFramesCounter() {
        {
            std::lock_guard<std::mutex> lock{frames_mutex};
            --queued_frame_count;

            if (queued_frame_count) {
                return;
            }
        }
        frames_condition.notify_one();
    }

    void WaitForFrames() {
        {
            std::lock_guard<std::mutex> lock{frames_mutex};
            if (!queued_frame_count) {
                return;
            }
        }

        // Wait for the GPU to be idle (all commands to be executed)
        {
            std::unique_lock<std::mutex> lock{frames_mutex};
            frames_condition.wait(lock, [this] { return !queued_frame_count; });
        }
    }

    void SignalCommands() {
        {
            std::unique_lock<std::mutex> lock{commands_mutex};
            if (queue.Empty()) {
                return;
            }
        }

        commands_condition.notify_one();
    }

    void WaitForCommands() {
        std::unique_lock<std::mutex> lock{commands_mutex};
        commands_condition.wait(lock, [this] { return !queue.Empty(); });
    }

    using CommandQueue = Common::SPSCQueue<CommandDataContainer>;
    CommandQueue queue;
};

/// Class used to manage the GPU thread
class ThreadManager final {
public:
    explicit ThreadManager(VideoCore::RendererBase& renderer, Tegra::DmaPusher& dma_pusher);
    ~ThreadManager();

    /// Push GPU command entries to be processed
    void SubmitList(Tegra::CommandList&& entries);

    /// Swap buffers (render frame)
    void SwapBuffers(
        std::optional<std::reference_wrapper<const Tegra::FramebufferConfig>> framebuffer);

    /// Notify rasterizer that any caches of the specified region should be flushed to Switch memory
    void FlushRegion(CacheAddr addr, u64 size);

    /// Notify rasterizer that any caches of the specified region should be invalidated
    void InvalidateRegion(CacheAddr addr, u64 size);

    /// Notify rasterizer that any caches of the specified region should be flushed and invalidated
    void FlushAndInvalidateRegion(CacheAddr addr, u64 size);

private:
    /// Pushes a command to be executed by the GPU thread
    void PushCommand(CommandData&& command_data);

private:
    SynchState state;
    VideoCore::RendererBase& renderer;
    Tegra::DmaPusher& dma_pusher;
    std::thread thread;
    std::thread::id thread_id;
};

} // namespace VideoCommon::GPUThread
