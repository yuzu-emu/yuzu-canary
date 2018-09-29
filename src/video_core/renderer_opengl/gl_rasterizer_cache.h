// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <map>
#include <memory>
#include <vector>

#include "common/alignment.h"
#include "common/common_types.h"
#include "common/hash.h"
#include "common/math_util.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/rasterizer_cache.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_shader_gen.h"
#include "video_core/textures/texture.h"

namespace OpenGL {

class CachedSurface;
using Surface = std::shared_ptr<CachedSurface>;
using SurfaceSurfaceRect_Tuple = std::tuple<Surface, Surface, MathUtil::Rectangle<u32>>;

struct SurfaceParams {
    enum class PixelFormat {
        ABGR8U = 0,
        ABGR8S = 1,
        ABGR8UI = 2,
        B5G6R5U = 3,
        A2B10G10R10U = 4,
        A1B5G5R5U = 5,
        R8U = 6,
        R8UI = 7,
        RGBA16F = 8,
        RGBA16U = 9,
        RGBA16UI = 10,
        R11FG11FB10F = 11,
        RGBA32UI = 12,
        DXT1 = 13,
        DXT23 = 14,
        DXT45 = 15,
        DXN1 = 16, // This is also known as BC4
        DXN2UNORM = 17,
        DXN2SNORM = 18,
        BC7U = 19,
        BC6H_UF16 = 20,
        BC6H_SF16 = 21,
        ASTC_2D_4X4 = 22,
        G8R8U = 23,
        G8R8S = 24,
        BGRA8 = 25,
        RGBA32F = 26,
        RG32F = 27,
        R32F = 28,
        R16F = 29,
        R16U = 30,
        R16S = 31,
        R16UI = 32,
        R16I = 33,
        RG16 = 34,
        RG16F = 35,
        RG16UI = 36,
        RG16I = 37,
        RG16S = 38,
        RGB32F = 39,
        SRGBA8 = 40,
        RG8U = 41,
        RG8S = 42,
        RG32UI = 43,
        R32UI = 44,
        ASTC_2D_8X8 = 45,

        MaxColorFormat,

        // Depth formats
        Z32F = 46,
        Z16 = 47,

        MaxDepthFormat,

        // DepthStencil formats
        Z24S8 = 48,
        S8Z24 = 49,
        Z32FS8 = 50,

        MaxDepthStencilFormat,

        Max = MaxDepthStencilFormat,
        Invalid = 255,
    };

    static constexpr std::size_t MaxPixelFormat = static_cast<std::size_t>(PixelFormat::Max);

    enum class ComponentType {
        Invalid = 0,
        SNorm = 1,
        UNorm = 2,
        SInt = 3,
        UInt = 4,
        Float = 5,
    };

    enum class SurfaceType {
        ColorTexture = 0,
        Depth = 1,
        DepthStencil = 2,
        Fill = 3,
        Invalid = 4,
    };

    enum class SurfaceTarget {
        Texture1D,
        Texture2D,
        Texture3D,
        Texture1DArray,
        Texture2DArray,
        TextureCubemap,
    };

    static SurfaceTarget SurfaceTargetFromTextureType(Tegra::Texture::TextureType texture_type) {
        switch (texture_type) {
        case Tegra::Texture::TextureType::Texture1D:
            return SurfaceTarget::Texture1D;
        case Tegra::Texture::TextureType::Texture2D:
        case Tegra::Texture::TextureType::Texture2DNoMipmap:
            return SurfaceTarget::Texture2D;
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

    static std::string SurfaceTargetName(SurfaceTarget target) {
        switch (target) {
        case SurfaceTarget::Texture1D:
            return "Texture1D";
        case SurfaceTarget::Texture2D:
            return "Texture2D";
        case SurfaceTarget::Texture3D:
            return "Texture3D";
        case SurfaceTarget::Texture1DArray:
            return "Texture1DArray";
        case SurfaceTarget::Texture2DArray:
            return "Texture2DArray";
        case SurfaceTarget::TextureCubemap:
            return "TextureCubemap";
        default:
            LOG_CRITICAL(HW_GPU, "Unimplemented surface_target={}", static_cast<u32>(target));
            UNREACHABLE();
            return fmt::format("TextureUnknown({})", static_cast<u32>(target));
        }
    }

    /**
     * Gets the compression factor for the specified PixelFormat. This applies to just the
     * "compressed width" and "compressed height", not the overall compression factor of a
     * compressed image. This is used for maintaining proper surface sizes for compressed
     * texture formats.
     */
    static constexpr u32 GetCompressionFactor(PixelFormat format) {
        if (format == PixelFormat::Invalid)
            return 0;

        constexpr std::array<u32, MaxPixelFormat> compression_factor_table = {{
            1, // ABGR8U
            1, // ABGR8S
            1, // ABGR8UI
            1, // B5G6R5U
            1, // A2B10G10R10U
            1, // A1B5G5R5U
            1, // R8U
            1, // R8UI
            1, // RGBA16F
            1, // RGBA16U
            1, // RGBA16UI
            1, // R11FG11FB10F
            1, // RGBA32UI
            4, // DXT1
            4, // DXT23
            4, // DXT45
            4, // DXN1
            4, // DXN2UNORM
            4, // DXN2SNORM
            4, // BC7U
            4, // BC6H_UF16
            4, // BC6H_SF16
            4, // ASTC_2D_4X4
            1, // G8R8U
            1, // G8R8S
            1, // BGRA8
            1, // RGBA32F
            1, // RG32F
            1, // R32F
            1, // R16F
            1, // R16U
            1, // R16S
            1, // R16UI
            1, // R16I
            1, // RG16
            1, // RG16F
            1, // RG16UI
            1, // RG16I
            1, // RG16S
            1, // RGB32F
            1, // SRGBA8
            1, // RG8U
            1, // RG8S
            1, // RG32UI
            1, // R32UI
            4, // ASTC_2D_8X8
            1, // Z32F
            1, // Z16
            1, // Z24S8
            1, // S8Z24
            1, // Z32FS8
        }};

        ASSERT(static_cast<std::size_t>(format) < compression_factor_table.size());
        return compression_factor_table[static_cast<std::size_t>(format)];
    }

    static constexpr u32 GetFormatBpp(PixelFormat format) {
        if (format == PixelFormat::Invalid)
            return 0;

        constexpr std::array<u32, MaxPixelFormat> bpp_table = {{
            32,  // ABGR8U
            32,  // ABGR8S
            32,  // ABGR8UI
            16,  // B5G6R5U
            32,  // A2B10G10R10U
            16,  // A1B5G5R5U
            8,   // R8U
            8,   // R8UI
            64,  // RGBA16F
            64,  // RGBA16U
            64,  // RGBA16UI
            32,  // R11FG11FB10F
            128, // RGBA32UI
            64,  // DXT1
            128, // DXT23
            128, // DXT45
            64,  // DXN1
            128, // DXN2UNORM
            128, // DXN2SNORM
            128, // BC7U
            128, // BC6H_UF16
            128, // BC6H_SF16
            32,  // ASTC_2D_4X4
            16,  // G8R8U
            16,  // G8R8S
            32,  // BGRA8
            128, // RGBA32F
            64,  // RG32F
            32,  // R32F
            16,  // R16F
            16,  // R16U
            16,  // R16S
            16,  // R16UI
            16,  // R16I
            32,  // RG16
            32,  // RG16F
            32,  // RG16UI
            32,  // RG16I
            32,  // RG16S
            96,  // RGB32F
            32,  // SRGBA8
            16,  // RG8U
            16,  // RG8S
            64,  // RG32UI
            32,  // R32UI
            16,  // ASTC_2D_8X8
            32,  // Z32F
            16,  // Z16
            32,  // Z24S8
            32,  // S8Z24
            64,  // Z32FS8
        }};

        ASSERT(static_cast<std::size_t>(format) < bpp_table.size());
        return bpp_table[static_cast<std::size_t>(format)];
    }

    u32 GetFormatBpp() const {
        return GetFormatBpp(pixel_format);
    }

    static PixelFormat PixelFormatFromDepthFormat(Tegra::DepthFormat format) {
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

    static PixelFormat PixelFormatFromRenderTargetFormat(Tegra::RenderTargetFormat format) {
        switch (format) {
        // TODO (Hexagon12): Converting SRGBA to RGBA is a hack and doesn't completely correct the
        // gamma.
        case Tegra::RenderTargetFormat::RGBA8_SRGB:
        case Tegra::RenderTargetFormat::RGBA8_UNORM:
            return PixelFormat::ABGR8U;
        case Tegra::RenderTargetFormat::RGBA8_SNORM:
            return PixelFormat::ABGR8S;
        case Tegra::RenderTargetFormat::RGBA8_UINT:
            return PixelFormat::ABGR8UI;
        case Tegra::RenderTargetFormat::BGRA8_SRGB:
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

    static PixelFormat PixelFormatFromTextureFormat(Tegra::Texture::TextureFormat format,
                                                    Tegra::Texture::ComponentType component_type) {
        // TODO(Subv): Properly implement this
        switch (format) {
        case Tegra::Texture::TextureFormat::A8R8G8B8:
            switch (component_type) {
            case Tegra::Texture::ComponentType::UNORM:
                return PixelFormat::ABGR8U;
            case Tegra::Texture::ComponentType::SNORM:
                return PixelFormat::ABGR8S;
            case Tegra::Texture::ComponentType::UINT:
                return PixelFormat::ABGR8UI;
            }
            LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}",
                         static_cast<u32>(component_type));
            UNREACHABLE();
        case Tegra::Texture::TextureFormat::B5G6R5:
            switch (component_type) {
            case Tegra::Texture::ComponentType::UNORM:
                return PixelFormat::B5G6R5U;
            }
            LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}",
                         static_cast<u32>(component_type));
            UNREACHABLE();
        case Tegra::Texture::TextureFormat::A2B10G10R10:
            switch (component_type) {
            case Tegra::Texture::ComponentType::UNORM:
                return PixelFormat::A2B10G10R10U;
            }
            LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}",
                         static_cast<u32>(component_type));
            UNREACHABLE();
        case Tegra::Texture::TextureFormat::A1B5G5R5:
            switch (component_type) {
            case Tegra::Texture::ComponentType::UNORM:
                return PixelFormat::A1B5G5R5U;
            }
            LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}",
                         static_cast<u32>(component_type));
            UNREACHABLE();
        case Tegra::Texture::TextureFormat::R8:
            switch (component_type) {
            case Tegra::Texture::ComponentType::UNORM:
                return PixelFormat::R8U;
            case Tegra::Texture::ComponentType::UINT:
                return PixelFormat::R8UI;
            }
            LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}",
                         static_cast<u32>(component_type));
            UNREACHABLE();
        case Tegra::Texture::TextureFormat::G8R8:
            switch (component_type) {
            case Tegra::Texture::ComponentType::UNORM:
                return PixelFormat::G8R8U;
            case Tegra::Texture::ComponentType::SNORM:
                return PixelFormat::G8R8S;
            }
            LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}",
                         static_cast<u32>(component_type));
            UNREACHABLE();
        case Tegra::Texture::TextureFormat::R16_G16_B16_A16:
            switch (component_type) {
            case Tegra::Texture::ComponentType::UNORM:
                return PixelFormat::RGBA16U;
            case Tegra::Texture::ComponentType::FLOAT:
                return PixelFormat::RGBA16F;
            }
            LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}",
                         static_cast<u32>(component_type));
            UNREACHABLE();
        case Tegra::Texture::TextureFormat::BF10GF11RF11:
            switch (component_type) {
            case Tegra::Texture::ComponentType::FLOAT:
                return PixelFormat::R11FG11FB10F;
            }
            LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}",
                         static_cast<u32>(component_type));
            UNREACHABLE();
        case Tegra::Texture::TextureFormat::R32_G32_B32_A32:
            switch (component_type) {
            case Tegra::Texture::ComponentType::FLOAT:
                return PixelFormat::RGBA32F;
            case Tegra::Texture::ComponentType::UINT:
                return PixelFormat::RGBA32UI;
            }
            LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}",
                         static_cast<u32>(component_type));
            UNREACHABLE();
        case Tegra::Texture::TextureFormat::R32_G32:
            switch (component_type) {
            case Tegra::Texture::ComponentType::FLOAT:
                return PixelFormat::RG32F;
            case Tegra::Texture::ComponentType::UINT:
                return PixelFormat::RG32UI;
            }
            LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}",
                         static_cast<u32>(component_type));
            UNREACHABLE();
        case Tegra::Texture::TextureFormat::R32_G32_B32:
            switch (component_type) {
            case Tegra::Texture::ComponentType::FLOAT:
                return PixelFormat::RGB32F;
            }
            LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}",
                         static_cast<u32>(component_type));
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
            LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}",
                         static_cast<u32>(component_type));
            UNREACHABLE();
        case Tegra::Texture::TextureFormat::R32:
            switch (component_type) {
            case Tegra::Texture::ComponentType::FLOAT:
                return PixelFormat::R32F;
            case Tegra::Texture::ComponentType::UINT:
                return PixelFormat::R32UI;
            }
            LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}",
                         static_cast<u32>(component_type));
            UNREACHABLE();
        case Tegra::Texture::TextureFormat::ZF32:
            return PixelFormat::Z32F;
        case Tegra::Texture::TextureFormat::Z16:
            return PixelFormat::Z16;
        case Tegra::Texture::TextureFormat::Z24S8:
            return PixelFormat::Z24S8;
        case Tegra::Texture::TextureFormat::DXT1:
            return PixelFormat::DXT1;
        case Tegra::Texture::TextureFormat::DXT23:
            return PixelFormat::DXT23;
        case Tegra::Texture::TextureFormat::DXT45:
            return PixelFormat::DXT45;
        case Tegra::Texture::TextureFormat::DXN1:
            return PixelFormat::DXN1;
        case Tegra::Texture::TextureFormat::DXN2:
            switch (component_type) {
            case Tegra::Texture::ComponentType::UNORM:
                return PixelFormat::DXN2UNORM;
            case Tegra::Texture::ComponentType::SNORM:
                return PixelFormat::DXN2SNORM;
            }
            LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}",
                         static_cast<u32>(component_type));
            UNREACHABLE();
        case Tegra::Texture::TextureFormat::BC7U:
            return PixelFormat::BC7U;
        case Tegra::Texture::TextureFormat::BC6H_UF16:
            return PixelFormat::BC6H_UF16;
        case Tegra::Texture::TextureFormat::BC6H_SF16:
            return PixelFormat::BC6H_SF16;
        case Tegra::Texture::TextureFormat::ASTC_2D_4X4:
            return PixelFormat::ASTC_2D_4X4;
        case Tegra::Texture::TextureFormat::ASTC_2D_8X8:
            return PixelFormat::ASTC_2D_8X8;
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
            LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}",
                         static_cast<u32>(component_type));
            UNREACHABLE();
        default:
            LOG_CRITICAL(HW_GPU, "Unimplemented format={}, component_type={}",
                         static_cast<u32>(format), static_cast<u32>(component_type));
            UNREACHABLE();
        }
    }

    static ComponentType ComponentTypeFromTexture(Tegra::Texture::ComponentType type) {
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

    static ComponentType ComponentTypeFromRenderTarget(Tegra::RenderTargetFormat format) {
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

    static PixelFormat PixelFormatFromGPUPixelFormat(Tegra::FramebufferConfig::PixelFormat format) {
        switch (format) {
        case Tegra::FramebufferConfig::PixelFormat::ABGR8:
            return PixelFormat::ABGR8U;
        default:
            LOG_CRITICAL(HW_GPU, "Unimplemented format={}", static_cast<u32>(format));
            UNREACHABLE();
        }
    }

    static ComponentType ComponentTypeFromDepthFormat(Tegra::DepthFormat format) {
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

    static SurfaceType GetFormatType(PixelFormat pixel_format) {
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

    /// Returns the rectangle corresponding to this surface
    MathUtil::Rectangle<u32> GetRect() const;

    /// Returns the size of this surface as a 2D texture in bytes, adjusted for compression
    std::size_t SizeInBytes2D() const {
        const u32 compression_factor{GetCompressionFactor(pixel_format)};
        ASSERT(width % compression_factor == 0);
        ASSERT(height % compression_factor == 0);
        return (width / compression_factor) * (height / compression_factor) *
               GetFormatBpp(pixel_format) / CHAR_BIT;
    }

    /// Returns the total size of this surface in bytes, adjusted for compression
    std::size_t SizeInBytesTotal() const {
        return SizeInBytes2D() * depth;
    }

    /**
     * Returns the size in bytes of the 2D surface with mipmaps. Each mipmap level proceeds the
     * previous with half the width and half the height. Once the size of the next mip reaches 0, we
     * are done.
     */
    std::size_t SizeInBytes2DWithMipmap() const {
        std::size_t size_in_bytes{};
        auto mip_params{*this};
        for (std::size_t level = 0; level < max_mip_level; level++) {
            size_in_bytes += mip_params.SizeInBytes2D();

            mip_params.width /= 2;
            mip_params.height /= 2;

            if (!mip_params.width || !mip_params.height) {
                break;
            }
        }

        // TODO(bunnei): This alignup is unverified, but necessary in games tested (e.g. in SMO)
        return Common::AlignUp(size_in_bytes, 0x1000);
    }

    /// Creates SurfaceParams from a texture configuration
    static SurfaceParams CreateForTexture(const Tegra::Texture::FullTextureInfo& config,
                                          const GLShader::SamplerEntry& entry);

    /// Creates SurfaceParams from a framebuffer configuration
    static SurfaceParams CreateForFramebuffer(std::size_t index);

    /// Creates SurfaceParams for a depth buffer configuration
    static SurfaceParams CreateForDepthBuffer(u32 zeta_width, u32 zeta_height,
                                              Tegra::GPUVAddr zeta_address,
                                              Tegra::DepthFormat format);

    /// Checks if surfaces are compatible for caching
    bool IsCompatibleSurface(const SurfaceParams& other) const {
        return std::tie(pixel_format, type, width, height, target, depth) ==
               std::tie(other.pixel_format, other.type, other.width, other.height, other.target,
                        other.depth);
    }

    VAddr addr;
    bool is_tiled;
    u32 block_height;
    PixelFormat pixel_format;
    ComponentType component_type;
    SurfaceType type;
    u32 width;
    u32 height;
    u32 depth;
    u32 unaligned_height;
    std::size_t size_in_bytes_total;
    std::size_t size_in_bytes_2d;
    SurfaceTarget target;
    u32 max_mip_level;

    // Render target specific parameters, not used in caching
    struct {
        u32 index;
        u32 array_mode;
        u32 layer_stride;
        u32 base_layer;
    } rt;
};

}; // namespace OpenGL

/// Hashable variation of SurfaceParams, used for a key in the surface cache
struct SurfaceReserveKey : Common::HashableStruct<OpenGL::SurfaceParams> {
    static SurfaceReserveKey Create(const OpenGL::SurfaceParams& params) {
        SurfaceReserveKey res;
        res.state = params;
        res.state.rt = {}; // Ignore rt config in caching
        return res;
    }
};
namespace std {
template <>
struct hash<SurfaceReserveKey> {
    std::size_t operator()(const SurfaceReserveKey& k) const {
        return k.Hash();
    }
};
} // namespace std

namespace OpenGL {

class CachedSurface final {
public:
    CachedSurface(const SurfaceParams& params);

    VAddr GetAddr() const {
        return params.addr;
    }

    std::size_t GetSizeInBytes() const {
        return params.size_in_bytes_total;
    }

    const OGLTexture& Texture() const {
        return texture;
    }

    GLenum Target() const {
        return gl_target;
    }

    static constexpr unsigned int GetGLBytesPerPixel(SurfaceParams::PixelFormat format) {
        if (format == SurfaceParams::PixelFormat::Invalid)
            return 0;

        return SurfaceParams::GetFormatBpp(format) / CHAR_BIT;
    }

    const SurfaceParams& GetSurfaceParams() const {
        return params;
    }

    // Read/Write data in Switch memory to/from gl_buffer
    void LoadGLBuffer();
    void FlushGLBuffer();

    // Upload data in gl_buffer to this surface's texture
    void UploadGLTexture(GLuint read_fb_handle, GLuint draw_fb_handle);

private:
    OGLTexture texture;
    std::vector<u8> gl_buffer;
    SurfaceParams params;
    GLenum gl_target;
};

class RasterizerCacheOpenGL final : public RasterizerCache<Surface> {
public:
    RasterizerCacheOpenGL();

    /// Get a surface based on the texture configuration
    Surface GetTextureSurface(const Tegra::Texture::FullTextureInfo& config,
                              const GLShader::SamplerEntry& entry);

    /// Get the depth surface based on the framebuffer configuration
    Surface GetDepthBufferSurface(bool preserve_contents);

    /// Get the color surface based on the framebuffer configuration and the specified render target
    Surface GetColorBufferSurface(std::size_t index, bool preserve_contents);

    /// Flushes the surface to Switch memory
    void FlushSurface(const Surface& surface);

    /// Tries to find a framebuffer using on the provided CPU address
    Surface TryFindFramebufferSurface(VAddr addr) const;

private:
    void LoadSurface(const Surface& surface);
    Surface GetSurface(const SurfaceParams& params, bool preserve_contents = true);

    /// Gets an uncached surface, creating it if need be
    Surface GetUncachedSurface(const SurfaceParams& params);

    /// Recreates a surface with new parameters
    Surface RecreateSurface(const Surface& old_surface, const SurfaceParams& new_params);

    /// Reserves a unique surface that can be reused later
    void ReserveSurface(const Surface& surface);

    /// Tries to get a reserved surface for the specified parameters
    Surface TryGetReservedSurface(const SurfaceParams& params);

    /// The surface reserve is a "backup" cache, this is where we put unique surfaces that have
    /// previously been used. This is to prevent surfaces from being constantly created and
    /// destroyed when used with different surface parameters.
    std::unordered_map<SurfaceReserveKey, Surface> surface_reserve;

    OGLFramebuffer read_framebuffer;
    OGLFramebuffer draw_framebuffer;

    /// Use a Pixel Buffer Object to download the previous texture and then upload it to the new one
    /// using the new format.
    OGLBuffer copy_pbo;
};

} // namespace OpenGL
