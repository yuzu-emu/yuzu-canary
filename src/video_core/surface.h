// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <climits>
#include <utility>
#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "video_core/gpu.h"
#include "video_core/textures/texture.h"

namespace VideoCore::Surface {

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
    RGBA8_SRGB = 40,
    RG8U = 41,
    RG8S = 42,
    RG32UI = 43,
    R32UI = 44,
    ASTC_2D_8X8 = 45,
    ASTC_2D_8X5 = 46,
    ASTC_2D_5X4 = 47,
    BGRA8_SRGB = 48,
    DXT1_SRGB = 49,
    DXT23_SRGB = 50,
    DXT45_SRGB = 51,
    BC7U_SRGB = 52,
    ASTC_2D_4X4_SRGB = 53,
    ASTC_2D_8X8_SRGB = 54,
    ASTC_2D_8X5_SRGB = 55,
    ASTC_2D_5X4_SRGB = 56,
    ASTC_2D_5X5 = 57,
    ASTC_2D_5X5_SRGB = 58,
    ASTC_2D_10X8 = 59,
    ASTC_2D_10X8_SRGB = 60,

    MaxColorFormat,

    // Depth formats
    Z32F = 61,
    Z16 = 62,

    MaxDepthFormat,

    // DepthStencil formats
    Z24S8 = 63,
    S8Z24 = 64,
    Z32FS8 = 65,

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
    TextureCubeArray,
};

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
        1, // RGBA8_SRGB
        1, // RG8U
        1, // RG8S
        1, // RG32UI
        1, // R32UI
        4, // ASTC_2D_8X8
        4, // ASTC_2D_8X5
        4, // ASTC_2D_5X4
        1, // BGRA8_SRGB
        4, // DXT1_SRGB
        4, // DXT23_SRGB
        4, // DXT45_SRGB
        4, // BC7U_SRGB
        4, // ASTC_2D_4X4_SRGB
        4, // ASTC_2D_8X8_SRGB
        4, // ASTC_2D_8X5_SRGB
        4, // ASTC_2D_5X4_SRGB
        4, // ASTC_2D_5X5
        4, // ASTC_2D_5X5_SRGB
        4, // ASTC_2D_10X8
        4, // ASTC_2D_10X8_SRGB
        1, // Z32F
        1, // Z16
        1, // Z24S8
        1, // S8Z24
        1, // Z32FS8
    }};

    ASSERT(static_cast<std::size_t>(format) < compression_factor_table.size());
    return compression_factor_table[static_cast<std::size_t>(format)];
}

static constexpr u32 GetDefaultBlockWidth(PixelFormat format) {
    if (format == PixelFormat::Invalid)
        return 0;
    constexpr std::array<u32, MaxPixelFormat> block_width_table = {{
        1,  // ABGR8U
        1,  // ABGR8S
        1,  // ABGR8UI
        1,  // B5G6R5U
        1,  // A2B10G10R10U
        1,  // A1B5G5R5U
        1,  // R8U
        1,  // R8UI
        1,  // RGBA16F
        1,  // RGBA16U
        1,  // RGBA16UI
        1,  // R11FG11FB10F
        1,  // RGBA32UI
        4,  // DXT1
        4,  // DXT23
        4,  // DXT45
        4,  // DXN1
        4,  // DXN2UNORM
        4,  // DXN2SNORM
        4,  // BC7U
        4,  // BC6H_UF16
        4,  // BC6H_SF16
        4,  // ASTC_2D_4X4
        1,  // G8R8U
        1,  // G8R8S
        1,  // BGRA8
        1,  // RGBA32F
        1,  // RG32F
        1,  // R32F
        1,  // R16F
        1,  // R16U
        1,  // R16S
        1,  // R16UI
        1,  // R16I
        1,  // RG16
        1,  // RG16F
        1,  // RG16UI
        1,  // RG16I
        1,  // RG16S
        1,  // RGB32F
        1,  // RGBA8_SRGB
        1,  // RG8U
        1,  // RG8S
        1,  // RG32UI
        1,  // R32UI
        8,  // ASTC_2D_8X8
        8,  // ASTC_2D_8X5
        5,  // ASTC_2D_5X4
        1,  // BGRA8_SRGB
        4,  // DXT1_SRGB
        4,  // DXT23_SRGB
        4,  // DXT45_SRGB
        4,  // BC7U_SRGB
        4,  // ASTC_2D_4X4_SRGB
        8,  // ASTC_2D_8X8_SRGB
        8,  // ASTC_2D_8X5_SRGB
        5,  // ASTC_2D_5X4_SRGB
        5,  // ASTC_2D_5X5
        5,  // ASTC_2D_5X5_SRGB
        10, // ASTC_2D_10X8
        10, // ASTC_2D_10X8_SRGB
        1,  // Z32F
        1,  // Z16
        1,  // Z24S8
        1,  // S8Z24
        1,  // Z32FS8
    }};
    ASSERT(static_cast<std::size_t>(format) < block_width_table.size());
    return block_width_table[static_cast<std::size_t>(format)];
}

static constexpr u32 GetDefaultBlockHeight(PixelFormat format) {
    if (format == PixelFormat::Invalid)
        return 0;

    constexpr std::array<u32, MaxPixelFormat> block_height_table = {{
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
        1, // RGBA8_SRGB
        1, // RG8U
        1, // RG8S
        1, // RG32UI
        1, // R32UI
        8, // ASTC_2D_8X8
        5, // ASTC_2D_8X5
        4, // ASTC_2D_5X4
        1, // BGRA8_SRGB
        4, // DXT1_SRGB
        4, // DXT23_SRGB
        4, // DXT45_SRGB
        4, // BC7U_SRGB
        4, // ASTC_2D_4X4_SRGB
        8, // ASTC_2D_8X8_SRGB
        5, // ASTC_2D_8X5_SRGB
        4, // ASTC_2D_5X4_SRGB
        5, // ASTC_2D_5X5
        5, // ASTC_2D_5X5_SRGB
        8, // ASTC_2D_10X8
        8, // ASTC_2D_10X8_SRGB
        1, // Z32F
        1, // Z16
        1, // Z24S8
        1, // S8Z24
        1, // Z32FS8
    }};

    ASSERT(static_cast<std::size_t>(format) < block_height_table.size());
    return block_height_table[static_cast<std::size_t>(format)];
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
        128, // ASTC_2D_4X4
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
        32,  // RGBA8_SRGB
        16,  // RG8U
        16,  // RG8S
        64,  // RG32UI
        32,  // R32UI
        128, // ASTC_2D_8X8
        128, // ASTC_2D_8X5
        128, // ASTC_2D_5X4
        32,  // BGRA8_SRGB
        64,  // DXT1_SRGB
        128, // DXT23_SRGB
        128, // DXT45_SRGB
        128, // BC7U
        128, // ASTC_2D_4X4_SRGB
        128, // ASTC_2D_8X8_SRGB
        128, // ASTC_2D_8X5_SRGB
        128, // ASTC_2D_5X4_SRGB
        128, // ASTC_2D_5X5
        128, // ASTC_2D_5X5_SRGB
        128, // ASTC_2D_10X8
        128, // ASTC_2D_10X8_SRGB
        32,  // Z32F
        16,  // Z16
        32,  // Z24S8
        32,  // S8Z24
        64,  // Z32FS8
    }};

    ASSERT(static_cast<std::size_t>(format) < bpp_table.size());
    return bpp_table[static_cast<std::size_t>(format)];
}

/// Returns the sizer in bytes of the specified pixel format
static constexpr u32 GetBytesPerPixel(PixelFormat pixel_format) {
    if (pixel_format == PixelFormat::Invalid) {
        return 0;
    }
    return GetFormatBpp(pixel_format) / CHAR_BIT;
}

SurfaceTarget SurfaceTargetFromTextureType(Tegra::Texture::TextureType texture_type);

bool SurfaceTargetIsLayered(SurfaceTarget target);

PixelFormat PixelFormatFromDepthFormat(Tegra::DepthFormat format);

PixelFormat PixelFormatFromRenderTargetFormat(Tegra::RenderTargetFormat format);

PixelFormat PixelFormatFromTextureFormat(Tegra::Texture::TextureFormat format,
                                         Tegra::Texture::ComponentType component_type,
                                         bool is_srgb);

ComponentType ComponentTypeFromTexture(Tegra::Texture::ComponentType type);

ComponentType ComponentTypeFromRenderTarget(Tegra::RenderTargetFormat format);

PixelFormat PixelFormatFromGPUPixelFormat(Tegra::FramebufferConfig::PixelFormat format);

ComponentType ComponentTypeFromDepthFormat(Tegra::DepthFormat format);

SurfaceType GetFormatType(PixelFormat pixel_format);

bool IsPixelFormatASTC(PixelFormat format);

std::pair<u32, u32> GetASTCBlockSize(PixelFormat format);

/// Returns true if the specified PixelFormat is a BCn format, e.g. DXT or DXN
bool IsFormatBCn(PixelFormat format);

} // namespace VideoCore::Surface
