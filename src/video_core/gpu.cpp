// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/microprofile.h"
#include "video_core/engines/fermi_2d.h"
#include "video_core/engines/kepler_memory.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/engines/maxwell_compute.h"
#include "video_core/engines/maxwell_dma.h"
#include "video_core/gpu.h"
#include "video_core/rasterizer_interface.h"

namespace Tegra {

u32 FramebufferConfig::BytesPerPixel(PixelFormat format) {
    switch (format) {
    case PixelFormat::ABGR8:
        return 4;
    default:
        return 4;
    }

    UNREACHABLE();
}

GPU::GPU(VideoCore::RasterizerInterface& rasterizer) {
    memory_manager = std::make_unique<Tegra::MemoryManager>();
    dma_pusher = std::make_unique<Tegra::DmaPusher>(*this);
    maxwell_3d = std::make_unique<Engines::Maxwell3D>(rasterizer, *memory_manager);
    fermi_2d = std::make_unique<Engines::Fermi2D>(rasterizer, *memory_manager);
    maxwell_compute = std::make_unique<Engines::MaxwellCompute>();
    maxwell_dma = std::make_unique<Engines::MaxwellDMA>(rasterizer, *memory_manager);
    kepler_memory = std::make_unique<Engines::KeplerMemory>(rasterizer, *memory_manager);
}

GPU::~GPU() = default;

Engines::Maxwell3D& GPU::Maxwell3D() {
    return *maxwell_3d;
}

const Engines::Maxwell3D& GPU::Maxwell3D() const {
    return *maxwell_3d;
}

MemoryManager& GPU::MemoryManager() {
    return *memory_manager;
}

const MemoryManager& GPU::MemoryManager() const {
    return *memory_manager;
}

DmaPusher& GPU::DmaPusher() {
    return *dma_pusher;
}

const DmaPusher& GPU::DmaPusher() const {
    return *dma_pusher;
}

u32 RenderTargetBytesPerPixel(RenderTargetFormat format) {
    ASSERT(format != RenderTargetFormat::NONE);

    switch (format) {
    case RenderTargetFormat::RGBA32_FLOAT:
    case RenderTargetFormat::RGBA32_UINT:
        return 16;
    case RenderTargetFormat::RGBA16_UINT:
    case RenderTargetFormat::RGBA16_UNORM:
    case RenderTargetFormat::RGBA16_FLOAT:
    case RenderTargetFormat::RG32_FLOAT:
    case RenderTargetFormat::RG32_UINT:
        return 8;
    case RenderTargetFormat::RGBA8_UNORM:
    case RenderTargetFormat::RGBA8_SNORM:
    case RenderTargetFormat::RGBA8_SRGB:
    case RenderTargetFormat::RGBA8_UINT:
    case RenderTargetFormat::RGB10_A2_UNORM:
    case RenderTargetFormat::BGRA8_UNORM:
    case RenderTargetFormat::BGRA8_SRGB:
    case RenderTargetFormat::RG16_UNORM:
    case RenderTargetFormat::RG16_SNORM:
    case RenderTargetFormat::RG16_UINT:
    case RenderTargetFormat::RG16_SINT:
    case RenderTargetFormat::RG16_FLOAT:
    case RenderTargetFormat::R32_FLOAT:
    case RenderTargetFormat::R11G11B10_FLOAT:
    case RenderTargetFormat::R32_UINT:
        return 4;
    case RenderTargetFormat::R16_UNORM:
    case RenderTargetFormat::R16_SNORM:
    case RenderTargetFormat::R16_UINT:
    case RenderTargetFormat::R16_SINT:
    case RenderTargetFormat::R16_FLOAT:
    case RenderTargetFormat::RG8_UNORM:
    case RenderTargetFormat::RG8_SNORM:
        return 2;
    case RenderTargetFormat::R8_UNORM:
    case RenderTargetFormat::R8_UINT:
        return 1;
    default:
        UNIMPLEMENTED_MSG("Unimplemented render target format {}", static_cast<u32>(format));
    }
}

u32 DepthFormatBytesPerPixel(DepthFormat format) {
    switch (format) {
    case DepthFormat::Z32_S8_X24_FLOAT:
        return 8;
    case DepthFormat::Z32_FLOAT:
    case DepthFormat::S8_Z24_UNORM:
    case DepthFormat::Z24_X8_UNORM:
    case DepthFormat::Z24_S8_UNORM:
    case DepthFormat::Z24_C8_UNORM:
        return 4;
    case DepthFormat::Z16_UNORM:
        return 2;
    default:
        UNIMPLEMENTED_MSG("Unimplemented Depth format {}", static_cast<u32>(format));
    }
}

enum class BufferMethods {
    BindObject = 0,
    CountBufferMethods = 0x40,
};

MICROPROFILE_DEFINE(ProcessCommandLists, "GPU", "Execute command buffer", MP_RGB(128, 128, 192));

void GPU::CallMethod(const MethodCall& method_call) {
    MICROPROFILE_SCOPE(ProcessCommandLists);

    LOG_TRACE(HW_GPU,
              "Processing method {:08X} on subchannel {} value "
              "{:08X} remaining params {}",
              MethCall.method, MethCall.subchannel, value, remaining_params);

    ASSERT(method_call.subchannel < bound_engines.size());

    if (method_call.method == static_cast<u32>(BufferMethods::BindObject)) {
        // Bind the current subchannel to the desired engine id.
        LOG_DEBUG(HW_GPU, "Binding subchannel {} to engine {}", method_call.subchannel,
                  method_call.argument);
        bound_engines[method_call.subchannel] = static_cast<EngineID>(method_call.argument);
        return;
    }

    const EngineID engine = bound_engines[method_call.subchannel];

    switch (engine) {
    case EngineID::FERMI_TWOD_A:
        fermi_2d->CallMethod(method_call);
        break;
    case EngineID::MAXWELL_B:
        maxwell_3d->CallMethod(method_call);
        break;
    case EngineID::MAXWELL_COMPUTE_B:
        maxwell_compute->CallMethod(method_call);
        break;
    case EngineID::MAXWELL_DMA_COPY_A:
        maxwell_dma->CallMethod(method_call);
        break;
    case EngineID::KEPLER_INLINE_TO_MEMORY_B:
        kepler_memory->CallMethod(method_call);
        break;
    default:
        UNIMPLEMENTED_MSG("Unimplemented engine");
    }
}

} // namespace Tegra
