// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "video_core/engines/fermi_2d.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/engines/maxwell_compute.h"
#include "video_core/engines/maxwell_dma.h"
#include "video_core/gpu.h"
#include "video_core/rasterizer_interface.h"

namespace Tegra {

GPU::GPU(VideoCore::RasterizerInterface& rasterizer) {
    memory_manager = std::make_unique<MemoryManager>();
    maxwell_3d = std::make_unique<Engines::Maxwell3D>(rasterizer, *memory_manager);
    fermi_2d = std::make_unique<Engines::Fermi2D>(*memory_manager);
    maxwell_compute = std::make_unique<Engines::MaxwellCompute>();
    maxwell_dma = std::make_unique<Engines::MaxwellDMA>(*memory_manager);
}

GPU::~GPU() = default;

const Engines::Maxwell3D& GPU::Maxwell3D() const {
    return *maxwell_3d;
}

Engines::Maxwell3D& GPU::Maxwell3D() {
    return *maxwell_3d;
}

u32 RenderTargetBytesPerPixel(RenderTargetFormat format) {
    ASSERT(format != RenderTargetFormat::NONE);

    switch (format) {
    case RenderTargetFormat::RGBA32_FLOAT:
        return 16;
    case RenderTargetFormat::RGBA16_FLOAT:
    case RenderTargetFormat::RG32_FLOAT:
        return 8;
    case RenderTargetFormat::RGBA8_UNORM:
    case RenderTargetFormat::RGB10_A2_UNORM:
    case RenderTargetFormat::BGRA8_UNORM:
    case RenderTargetFormat::R32_FLOAT:
    case RenderTargetFormat::R11G11B10_FLOAT:
        return 4;
    default:
        UNIMPLEMENTED_MSG("Unimplemented render target format {}", static_cast<u32>(format));
    }
}

} // namespace Tegra
