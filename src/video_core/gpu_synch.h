// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "video_core/gpu.h"

namespace VideoCore {
class RendererBase;
} // namespace VideoCore

namespace VideoCommon {

/// Implementation of GPU interface that runs the GPU synchronously
class GPUSynch : public Tegra::GPU {
public:
    explicit GPUSynch(Core::System& system, VideoCore::RendererBase& renderer);
    ~GPUSynch() override;

    void PushGPUEntries(Tegra::CommandList&& entries) override;
    void SwapBuffers(
        std::optional<std::reference_wrapper<const Tegra::FramebufferConfig>> framebuffer) override;
    void FlushRegion(CacheAddr addr, u64 size) override;
    void InvalidateRegion(CacheAddr addr, u64 size) override;
    void FlushAndInvalidateRegion(CacheAddr addr, u64 size) override;
};

} // namespace VideoCommon
