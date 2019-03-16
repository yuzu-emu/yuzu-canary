// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "video_core/gpu.h"
#include "video_core/gpu_thread.h"

namespace VideoCore {
class RendererBase;
} // namespace VideoCore

namespace VideoCommon {

namespace GPUThread {
class ThreadManager;
} // namespace GPUThread

/// Implementation of GPU interface that runs the GPU asynchronously
class GPUAsynch : public Tegra::GPU {
public:
    explicit GPUAsynch(Core::System& system, VideoCore::RendererBase& renderer);
    ~GPUAsynch() override;

    void PushGPUEntries(Tegra::CommandList&& entries) override;
    void SwapBuffers(
        std::optional<std::reference_wrapper<const Tegra::FramebufferConfig>> framebuffer) override;
    void FlushRegion(CacheAddr addr, u64 size) override;
    void InvalidateRegion(CacheAddr addr, u64 size) override;
    void FlushAndInvalidateRegion(CacheAddr addr, u64 size) override;

private:
    GPUThread::ThreadManager gpu_thread;
};

} // namespace VideoCommon
