// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "video_core/gpu_asynch.h"
#include "video_core/gpu_thread.h"
#include "video_core/renderer_base.h"

namespace VideoCommon {

GPUAsynch::GPUAsynch(Core::System& system, VideoCore::RendererBase& renderer)
    : Tegra::GPU(system, renderer), gpu_thread{renderer, *dma_pusher} {}

GPUAsynch::~GPUAsynch() = default;

void GPUAsynch::PushGPUEntries(Tegra::CommandList&& entries) {
    gpu_thread.SubmitList(std::move(entries));
}

void GPUAsynch::SwapBuffers(
    std::optional<std::reference_wrapper<const Tegra::FramebufferConfig>> framebuffer) {
    gpu_thread.SwapBuffers(std::move(framebuffer));
}

void GPUAsynch::FlushRegion(CacheAddr addr, u64 size) {
    gpu_thread.FlushRegion(addr, size);
}

void GPUAsynch::InvalidateRegion(CacheAddr addr, u64 size) {
    gpu_thread.InvalidateRegion(addr, size);
}

void GPUAsynch::FlushAndInvalidateRegion(CacheAddr addr, u64 size) {
    gpu_thread.FlushAndInvalidateRegion(addr, size);
}

} // namespace VideoCommon
