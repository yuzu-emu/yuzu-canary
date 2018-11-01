// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/common_types.h"
#include "common/math_util.h"
#include "video_core/surface.h"

namespace VideoCore::Surface {

SurfaceTarget SurfaceTargetFromTextureType(Tegra::Texture::TextureType texture_type) {
    switch (texture_type) {
    case Tegra::Texture::TextureType::Texture1D:
        return SurfaceTarget::Texture1D;
    case Tegra::Texture::TextureType::Texture2D:
    case Tegra::Texture::TextureType::Texture2DNoMipmap:
        return SurfaceTarget::Texture2D;
    case Tegra::Texture::TextureType::Texture3D:
        return SurfaceTarget::Texture3D;
    case Tegra::Texture::TextureType::TextureCubemap:
        return SurfaceTarget::TextureCubemap;
    case Tegra::Texture::TextureType::Texture1DArray:
        return SurfaceTarget::Texture1DArray;
    case Tegra::Texture::TextureType::Texture2DArray:
        return SurfaceTarget::Texture2DArray;
    default:
        LOG_CRITICAL(HW_GPU, "Unimplemented texture_type={}", static_cast<u32>(texture_type));
        UNREACHABLE();
        return SurfaceTarget::Texture2D;
    }
}

bool SurfaceTargetIsLayered(SurfaceTarget target) {
    switch (target) {
    case SurfaceTarget::Texture1D:
    case SurfaceTarget::Texture2D:
    case SurfaceTarget::Texture3D:
        return false;
    case SurfaceTarget::Texture1DArray:
    case SurfaceTarget::Texture2DArray:
    case SurfaceTarget::TextureCubemap:
        return true;
    default:
        LOG_CRITICAL(HW_GPU, "Unimplemented surface_target={}", static_cast<u32>(target));
        UNREACHABLE();
        return false;
    }
}

PixelFormat PixelFormatFromDepthFormat(Tegra::DepthFormat format) {
    switch (format) {
    case Tegra::DepthFormat::S8_Z24_UNORM:
        return PixelFormat::S8Z24;
    case Tegra::DepthFormat::Z24_S8_UNORM:
        return PixelFormat::Z24S8;
    case Tegra::DepthFormat::Z32_FLOAT:
        return PixelFormat::Z32F;
    case Tegra::DepthFormat::Z16_UNORM:
        return PixelFormat::Z16;
    case Tegra::DepthFormat::Z32_S8_X24_FLOAT:
        return PixelFormat::Z32FS8;
    default:
        LOG_CRITICAL(HW_GPU, "Unimplemented format={}", static_cast<u32>(format));
        UNREACHABLE();
    }
}

PixelFormat PixelFormatFromRenderTargetFormat(Tegra::RenderTargetFormat format) {
    switch (format) {
        // TODO (Hexagon12): Converting SRGBA to RGBA is a hack and doesn't completely correct the
        // gamma.
    case Tegra::RenderTargetFormat::RGBA8_SRGB:
        return PixelFormat::RGBA8_SRGB;
    case Tegra::RenderTargetFormat::RGBA8_UNORM:
        return PixelFormat::ABGR8U;
    case Tegra::RenderTargetFormat::RGBA8_SNORM:
        return PixelFormat::ABGR8S;
    case Tegra::RenderTargetFormat::RGBA8_UINT:
        return PixelFormat::ABGR8UI;
    case Tegra::RenderTargetFormat::BGRA8_SRGB:
        return PixelFormat::BGRA8_SRGB;
    case Tegra::RenderTargetFormat::BGRA8_UNORM:
        return PixelFormat::BGRA8;
    case Tegra::RenderTargetFormat::RGB10_A2_UNORM:
        return PixelFormat::A2B10G10R10U;
    case Tegra::RenderTargetFormat::RGBA16_FLOAT:
        return PixelFormat::RGBA16F;
    case Tegra::RenderTargetFormat::RGBA16_UNORM:
        return PixelFormat::RGBA16U;
    case Tegra::RenderTargetFormat::RGBA16_UINT:
        return PixelFormat::RGBA16UI;
    case Tegra::RenderTargetFormat::RGBA32_FLOAT:
        return PixelFormat::RGBA32F;
    case Tegra::RenderTargetFormat::RG32_FLOAT:
        return PixelFormat::RG32F;
    case Tegra::RenderTargetFormat::R11G11B10_FLOAT:
        return PixelFormat::R11FG11FB10F;
    case Tegra::RenderTargetFormat::B5G6R5_UNORM:
        return PixelFormat::B5G6R5U;
    case Tegra::RenderTargetFormat::BGR5A1_UNORM:
        return PixelFormat::A1B5G5R5U;
    case Tegra::RenderTargetFormat::RGBA32_UINT:
        return PixelFormat::RGBA32UI;
    case Tegra::RenderTargetFormat::R8_UNORM:
        return PixelFormat::R8U;
    case Tegra::RenderTargetFormat::R8_UINT:
        return PixelFormat::R8UI;
    case Tegra::RenderTargetFormat::RG16_FLOAT:
        return PixelFormat::RG16F;
    case Tegra::RenderTargetFormat::RG16_UINT:
        return PixelFormat::RG16UI;
    case Tegra::RenderTargetFormat::RG16_SINT:
        return PixelFormat::RG16I;
    case Tegra::RenderTargetFormat::RG16_UNORM:
        return PixelFormat::RG16;
    case Tegra::RenderTargetFormat::RG16_SNORM:
        return PixelFormat::RG16S;
    case Tegra::RenderTargetFormat::RG8_UNORM:
        return PixelFormat::RG8U;
    case Tegra::RenderTargetFormat::RG8_SNORM:
        return PixelFormat::RG8S;
    case Tegra::RenderTargetFormat::R16_FLOAT:
        return PixelFormat::R16F;
    case Tegra::RenderTargetFormat::R16_UNORM:
        return PixelFormat::R16U;
    case Tegra::RenderTargetFormat::R16_SNORM:
        return PixelFormat::R16S;
    case Tegra::RenderTargetFormat::R16_UINT:
        return PixelFormat::R16UI;
    case Tegra::RenderTargetFormat::R16_SINT:
        return PixelFormat::R16I;
    case Tegra::RenderTargetFormat::R32_FLOAT:
        return PixelFormat::R32F;
    case Tegra::RenderTargetFormat::R32_UINT:
        return PixelFormat::R32UI;
    case Tegra::RenderTargetFormat::RG32_UINT:
        return PixelFormat::RG32UI;
    default:
        LOG_CRITICAL(HW_GPU, "Unimplemented format={}", static_cast<u32>(format));
        UNREACHABLE();
    }
}

PixelFormat PixelFormatFromTextureFormat(Tegra::Texture::TextureFormat format,
                                         Tegra::Texture::ComponentType component_type,
                                         bool is_srgb) {
    // TODO(Subv): Properly implement this
    switch (format) {
    case Tegra::Texture::TextureFormat::A8R8G8B8:
        if (is_srgb) {
            return PixelFormat::RGBA8_SRGB;
        }
        switch (component_type) {
        case Tegra::Texture::ComponentType::UNORM:
            return PixelFormat::ABGR8U;
        case Tegra::Texture::ComponentType::SNORM:
            return PixelFormat::ABGR8S;
        case Tegra::Texture::ComponentType::UINT:
            return PixelFormat::ABGR8UI;
        }
        LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}", static_cast<u32>(component_type));
        UNREACHABLE();
    case Tegra::Texture::TextureFormat::B5G6R5:
        switch (component_type) {
        case Tegra::Texture::ComponentType::UNORM:
            return PixelFormat::B5G6R5U;
        }
        LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}", static_cast<u32>(component_type));
        UNREACHABLE();
    case Tegra::Texture::TextureFormat::A2B10G10R10:
        switch (component_type) {
        case Tegra::Texture::ComponentType::UNORM:
            return PixelFormat::A2B10G10R10U;
        }
        LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}", static_cast<u32>(component_type));
        UNREACHABLE();
    case Tegra::Texture::TextureFormat::A1B5G5R5:
        switch (component_type) {
        case Tegra::Texture::ComponentType::UNORM:
            return PixelFormat::A1B5G5R5U;
        }
        LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}", static_cast<u32>(component_type));
        UNREACHABLE();
    case Tegra::Texture::TextureFormat::R8:
        switch (component_type) {
        case Tegra::Texture::ComponentType::UNORM:
            return PixelFormat::R8U;
        case Tegra::Texture::ComponentType::UINT:
            return PixelFormat::R8UI;
        }
        LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}", static_cast<u32>(component_type));
        UNREACHABLE();
    case Tegra::Texture::TextureFormat::G8R8:
        switch (component_type) {
        case Tegra::Texture::ComponentType::UNORM:
            return PixelFormat::G8R8U;
        case Tegra::Texture::ComponentType::SNORM:
            return PixelFormat::G8R8S;
        }
        LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}", static_cast<u32>(component_type));
        UNREACHABLE();
    case Tegra::Texture::TextureFormat::R16_G16_B16_A16:
        switch (component_type) {
        case Tegra::Texture::ComponentType::UNORM:
            return PixelFormat::RGBA16U;
        case Tegra::Texture::ComponentType::FLOAT:
            return PixelFormat::RGBA16F;
        }
        LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}", static_cast<u32>(component_type));
        UNREACHABLE();
    case Tegra::Texture::TextureFormat::BF10GF11RF11:
        switch (component_type) {
        case Tegra::Texture::ComponentType::FLOAT:
            return PixelFormat::R11FG11FB10F;
        }
        LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}", static_cast<u32>(component_type));
        UNREACHABLE();
    case Tegra::Texture::TextureFormat::R32_G32_B32_A32:
        switch (component_type) {
        case Tegra::Texture::ComponentType::FLOAT:
            return PixelFormat::RGBA32F;
        case Tegra::Texture::ComponentType::UINT:
            return PixelFormat::RGBA32UI;
        }
        LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}", static_cast<u32>(component_type));
        UNREACHABLE();
    case Tegra::Texture::TextureFormat::R32_G32:
        switch (component_type) {
        case Tegra::Texture::ComponentType::FLOAT:
            return PixelFormat::RG32F;
        case Tegra::Texture::ComponentType::UINT:
            return PixelFormat::RG32UI;
        }
        LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}", static_cast<u32>(component_type));
        UNREACHABLE();
    case Tegra::Texture::TextureFormat::R32_G32_B32:
        switch (component_type) {
        case Tegra::Texture::ComponentType::FLOAT:
            return PixelFormat::RGB32F;
        }
        LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}", static_cast<u32>(component_type));
        UNREACHABLE();
    case Tegra::Texture::TextureFormat::R16:
        switch (component_type) {
        case Tegra::Texture::ComponentType::FLOAT:
            return PixelFormat::R16F;
        case Tegra::Texture::ComponentType::UNORM:
            return PixelFormat::R16U;
        case Tegra::Texture::ComponentType::SNORM:
            return PixelFormat::R16S;
        case Tegra::Texture::ComponentType::UINT:
            return PixelFormat::R16UI;
        case Tegra::Texture::ComponentType::SINT:
            return PixelFormat::R16I;
        }
        LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}", static_cast<u32>(component_type));
        UNREACHABLE();
    case Tegra::Texture::TextureFormat::R32:
        switch (component_type) {
        case Tegra::Texture::ComponentType::FLOAT:
            return PixelFormat::R32F;
        case Tegra::Texture::ComponentType::UINT:
            return PixelFormat::R32UI;
        }
        LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}", static_cast<u32>(component_type));
        UNREACHABLE();
    case Tegra::Texture::TextureFormat::ZF32:
        return PixelFormat::Z32F;
    case Tegra::Texture::TextureFormat::Z16:
        return PixelFormat::Z16;
    case Tegra::Texture::TextureFormat::Z24S8:
        return PixelFormat::Z24S8;
    case Tegra::Texture::TextureFormat::DXT1:
        return is_srgb ? PixelFormat::DXT1_SRGB : PixelFormat::DXT1;
    case Tegra::Texture::TextureFormat::DXT23:
        return is_srgb ? PixelFormat::DXT23_SRGB : PixelFormat::DXT23;
    case Tegra::Texture::TextureFormat::DXT45:
        return is_srgb ? PixelFormat::DXT45_SRGB : PixelFormat::DXT45;
    case Tegra::Texture::TextureFormat::DXN1:
        return PixelFormat::DXN1;
    case Tegra::Texture::TextureFormat::DXN2:
        switch (component_type) {
        case Tegra::Texture::ComponentType::UNORM:
            return PixelFormat::DXN2UNORM;
        case Tegra::Texture::ComponentType::SNORM:
            return PixelFormat::DXN2SNORM;
        }
        LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}", static_cast<u32>(component_type));
        UNREACHABLE();
    case Tegra::Texture::TextureFormat::BC7U:
        return is_srgb ? PixelFormat::BC7U_SRGB : PixelFormat::BC7U;
    case Tegra::Texture::TextureFormat::BC6H_UF16:
        return PixelFormat::BC6H_UF16;
    case Tegra::Texture::TextureFormat::BC6H_SF16:
        return PixelFormat::BC6H_SF16;
    case Tegra::Texture::TextureFormat::ASTC_2D_4X4:
        return is_srgb ? PixelFormat::ASTC_2D_4X4_SRGB : PixelFormat::ASTC_2D_4X4;
    case Tegra::Texture::TextureFormat::ASTC_2D_5X4:
        return is_srgb ? PixelFormat::ASTC_2D_5X4_SRGB : PixelFormat::ASTC_2D_5X4;
    case Tegra::Texture::TextureFormat::ASTC_2D_5X5:
        return is_srgb ? PixelFormat::ASTC_2D_5X5_SRGB : PixelFormat::ASTC_2D_5X5;
    case Tegra::Texture::TextureFormat::ASTC_2D_8X8:
        return is_srgb ? PixelFormat::ASTC_2D_8X8_SRGB : PixelFormat::ASTC_2D_8X8;
    case Tegra::Texture::TextureFormat::ASTC_2D_8X5:
        return is_srgb ? PixelFormat::ASTC_2D_8X5_SRGB : PixelFormat::ASTC_2D_8X5;
    case Tegra::Texture::TextureFormat::R16_G16:
        switch (component_type) {
        case Tegra::Texture::ComponentType::FLOAT:
            return PixelFormat::RG16F;
        case Tegra::Texture::ComponentType::UNORM:
            return PixelFormat::RG16;
        case Tegra::Texture::ComponentType::SNORM:
            return PixelFormat::RG16S;
        case Tegra::Texture::ComponentType::UINT:
            return PixelFormat::RG16UI;
        case Tegra::Texture::ComponentType::SINT:
            return PixelFormat::RG16I;
        }
        LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}", static_cast<u32>(component_type));
        UNREACHABLE();
    default:
        LOG_CRITICAL(HW_GPU, "Unimplemented format={}, component_type={}", static_cast<u32>(format),
                     static_cast<u32>(component_type));
        UNREACHABLE();
    }
}

ComponentType ComponentTypeFromTexture(Tegra::Texture::ComponentType type) {
    // TODO(Subv): Implement more component types
    switch (type) {
    case Tegra::Texture::ComponentType::UNORM:
        return ComponentType::UNorm;
    case Tegra::Texture::ComponentType::FLOAT:
        return ComponentType::Float;
    case Tegra::Texture::ComponentType::SNORM:
        return ComponentType::SNorm;
    case Tegra::Texture::ComponentType::UINT:
        return ComponentType::UInt;
    case Tegra::Texture::ComponentType::SINT:
        return ComponentType::SInt;
    default:
        LOG_CRITICAL(HW_GPU, "Unimplemented component type={}", static_cast<u32>(type));
        UNREACHABLE();
    }
}

ComponentType ComponentTypeFromRenderTarget(Tegra::RenderTargetFormat format) {
    // TODO(Subv): Implement more render targets
    switch (format) {
    case Tegra::RenderTargetFormat::RGBA8_UNORM:
    case Tegra::RenderTargetFormat::RGBA8_SRGB:
    case Tegra::RenderTargetFormat::BGRA8_UNORM:
    case Tegra::RenderTargetFormat::BGRA8_SRGB:
    case Tegra::RenderTargetFormat::RGB10_A2_UNORM:
    case Tegra::RenderTargetFormat::R8_UNORM:
    case Tegra::RenderTargetFormat::RG16_UNORM:
    case Tegra::RenderTargetFormat::R16_UNORM:
    case Tegra::RenderTargetFormat::B5G6R5_UNORM:
    case Tegra::RenderTargetFormat::BGR5A1_UNORM:
    case Tegra::RenderTargetFormat::RG8_UNORM:
    case Tegra::RenderTargetFormat::RGBA16_UNORM:
        return ComponentType::UNorm;
    case Tegra::RenderTargetFormat::RGBA8_SNORM:
    case Tegra::RenderTargetFormat::RG16_SNORM:
    case Tegra::RenderTargetFormat::R16_SNORM:
    case Tegra::RenderTargetFormat::RG8_SNORM:
        return ComponentType::SNorm;
    case Tegra::RenderTargetFormat::RGBA16_FLOAT:
    case Tegra::RenderTargetFormat::R11G11B10_FLOAT:
    case Tegra::RenderTargetFormat::RGBA32_FLOAT:
    case Tegra::RenderTargetFormat::RG32_FLOAT:
    case Tegra::RenderTargetFormat::RG16_FLOAT:
    case Tegra::RenderTargetFormat::R16_FLOAT:
    case Tegra::RenderTargetFormat::R32_FLOAT:
        return ComponentType::Float;
    case Tegra::RenderTargetFormat::RGBA32_UINT:
    case Tegra::RenderTargetFormat::RGBA16_UINT:
    case Tegra::RenderTargetFormat::RG16_UINT:
    case Tegra::RenderTargetFormat::R8_UINT:
    case Tegra::RenderTargetFormat::R16_UINT:
    case Tegra::RenderTargetFormat::RG32_UINT:
    case Tegra::RenderTargetFormat::R32_UINT:
    case Tegra::RenderTargetFormat::RGBA8_UINT:
        return ComponentType::UInt;
    case Tegra::RenderTargetFormat::RG16_SINT:
    case Tegra::RenderTargetFormat::R16_SINT:
        return ComponentType::SInt;
    default:
        LOG_CRITICAL(HW_GPU, "Unimplemented format={}", static_cast<u32>(format));
        UNREACHABLE();
    }
}

PixelFormat PixelFormatFromGPUPixelFormat(Tegra::FramebufferConfig::PixelFormat format) {
    switch (format) {
    case Tegra::FramebufferConfig::PixelFormat::ABGR8:
        return PixelFormat::ABGR8U;
    default:
        LOG_CRITICAL(HW_GPU, "Unimplemented format={}", static_cast<u32>(format));
        UNREACHABLE();
    }
}

ComponentType ComponentTypeFromDepthFormat(Tegra::DepthFormat format) {
    switch (format) {
    case Tegra::DepthFormat::Z16_UNORM:
    case Tegra::DepthFormat::S8_Z24_UNORM:
    case Tegra::DepthFormat::Z24_S8_UNORM:
        return ComponentType::UNorm;
    case Tegra::DepthFormat::Z32_FLOAT:
    case Tegra::DepthFormat::Z32_S8_X24_FLOAT:
        return ComponentType::Float;
    default:
        LOG_CRITICAL(HW_GPU, "Unimplemented format={}", static_cast<u32>(format));
        UNREACHABLE();
    }
}

SurfaceType GetFormatType(PixelFormat pixel_format) {
    if (static_cast<std::size_t>(pixel_format) <
        static_cast<std::size_t>(PixelFormat::MaxColorFormat)) {
        return SurfaceType::ColorTexture;
    }

    if (static_cast<std::size_t>(pixel_format) <
        static_cast<std::size_t>(PixelFormat::MaxDepthFormat)) {
        return SurfaceType::Depth;
    }

    if (static_cast<std::size_t>(pixel_format) <
        static_cast<std::size_t>(PixelFormat::MaxDepthStencilFormat)) {
        return SurfaceType::DepthStencil;
    }

    // TODO(Subv): Implement the other formats
    ASSERT(false);

    return SurfaceType::Invalid;
}

bool IsPixelFormatASTC(PixelFormat format) {
    switch (format) {
    case PixelFormat::ASTC_2D_4X4:
    case PixelFormat::ASTC_2D_5X4:
    case PixelFormat::ASTC_2D_5X5:
    case PixelFormat::ASTC_2D_8X8:
    case PixelFormat::ASTC_2D_8X5:
    case PixelFormat::ASTC_2D_4X4_SRGB:
    case PixelFormat::ASTC_2D_5X4_SRGB:
    case PixelFormat::ASTC_2D_5X5_SRGB:
    case PixelFormat::ASTC_2D_8X8_SRGB:
    case PixelFormat::ASTC_2D_8X5_SRGB:
        return true;
    default:
        return false;
    }
}

std::pair<u32, u32> GetASTCBlockSize(PixelFormat format) {
    return {GetDefaultBlockWidth(format), GetDefaultBlockHeight(format)};
}

bool IsFormatBCn(PixelFormat format) {
    switch (format) {
    case PixelFormat::DXT1:
    case PixelFormat::DXT23:
    case PixelFormat::DXT45:
    case PixelFormat::DXN1:
    case PixelFormat::DXN2SNORM:
    case PixelFormat::DXN2UNORM:
    case PixelFormat::BC7U:
    case PixelFormat::BC6H_UF16:
    case PixelFormat::BC6H_SF16:
    case PixelFormat::DXT1_SRGB:
    case PixelFormat::DXT23_SRGB:
    case PixelFormat::DXT45_SRGB:
    case PixelFormat::BC7U_SRGB:
        return true;
    }
    return false;
}

} // namespace VideoCore::Surface
