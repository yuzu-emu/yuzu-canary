// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include "core/core.h"
#include "core/settings.h"
#include "video_core/gpu_asynch.h"
#include "video_core/gpu_synch.h"
#include "video_core/renderer_base.h"
#include "video_core/renderer_opengl/renderer_opengl.h"
#include "video_core/video_core.h"

namespace VideoCore {

std::unique_ptr<RendererBase> CreateRenderer(Core::Frontend::EmuWindow& emu_window,
                                             Core::System& system) {
    return std::make_unique<OpenGL::RendererOpenGL>(emu_window, system);
}

std::unique_ptr<Tegra::GPU> CreateGPU(Core::System& system) {
    if (Settings::values.use_asynchronous_gpu_emulation) {
        return std::make_unique<VideoCommon::GPUAsynch>(system, system.Renderer());
    }

    return std::make_unique<VideoCommon::GPUSynch>(system, system.Renderer());
}

u16 GetResolutionScaleFactor(const RendererBase& renderer) {
    return static_cast<u16>(
        Settings::values.resolution_factor
            ? Settings::values.resolution_factor
            : renderer.GetRenderWindow().GetFramebufferLayout().GetScalingRatio());
}

} // namespace VideoCore
