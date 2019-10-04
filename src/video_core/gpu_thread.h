// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <condition_variable>
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

namespace Core {
class System;
} // namespace Core

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

    CommandDataContainer(CommandData&& data, u64 next_fence)
        : data{std::move(data)}, fence{next_fence} {}

    CommandData data;
    u64 fence{};
};

/// Struct used to synchronize the GPU thread
struct SynchState final {
    std::atomic_bool is_running{true};

    using CommandQueue = Common::SPSCQueue<CommandDataContainer>;
    CommandQueue queue;
    u64 last_fence{};
    std::atomic<u64> signaled_fence{};
};

/// Class used to manage the GPU thread
class ThreadManager final {
public:
    explicit ThreadManager(Core::System& system);
    ~ThreadManager();

    /// Creates and starts the GPU thread.
    void StartThread(VideoCore::RendererBase& renderer, Tegra::DmaPusher& dma_pusher);

    /// Push GPU command entries to be processed
    void SubmitList(Tegra::CommandList&& entries);

    /// Swap buffers (render frame)
    void SwapBuffers(const Tegra::FramebufferConfig* framebuffer);

    /// Notify rasterizer that any caches of the specified region should be flushed to Switch memory
    void FlushRegion(CacheAddr addr, u64 size);

    /// Notify rasterizer that any caches of the specified region should be invalidated
    void InvalidateRegion(CacheAddr addr, u64 size);

    /// Notify rasterizer that any caches of the specified region should be flushed and invalidated
    void FlushAndInvalidateRegion(CacheAddr addr, u64 size);

    // Wait until the gpu thread is idle.
    void WaitIdle() const;

private:
    /// Pushes a command to be executed by the GPU thread
    u64 PushCommand(CommandData&& command_data);

private:
    SynchState state;
    Core::System& system;
    std::thread thread;
    std::thread::id thread_id;
};

} // namespace VideoCommon::GPUThread
