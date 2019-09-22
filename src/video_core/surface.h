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
    BGRA8 = 23,
    RGBA32F = 24,
    RG32F = 25,
    R32F = 26,
    R16F = 27,
    R16U = 28,
    R16S = 29,
    R16UI = 30,
    R16I = 31,
    RG16 = 32,
    RG16F = 33,
    RG16UI = 34,
    RG16I = 35,
    RG16S = 36,
    RGB32F = 37,
    RGBA8_SRGB = 38,
    RG8U = 39,
    RG8S = 40,
    RG32UI = 41,
    RGBX16F = 42,
    R32UI = 43,
    ASTC_2D_8X8 = 44,
    ASTC_2D_8X5 = 45,
    ASTC_2D_5X4 = 46,
    BGRA8_SRGB = 47,
    DXT1_SRGB = 48,
    DXT23_SRGB = 49,
    DXT45_SRGB = 50,
    BC7U_SRGB = 51,
    ASTC_2D_4X4_SRGB = 52,
    ASTC_2D_8X8_SRGB = 53,
    ASTC_2D_8X5_SRGB = 54,
    ASTC_2D_5X4_SRGB = 55,
    ASTC_2D_5X5 = 56,
    ASTC_2D_5X5_SRGB = 57,
    ASTC_2D_10X8 = 58,
    ASTC_2D_10X8_SRGB = 59,

    MaxColorFormat,

    // Depth formats
    Z32F = 60,
    Z16 = 61,

    MaxDepthFormat,

    // DepthStencil formats
    Z24S8 = 62,
    S8Z24 = 63,
    Z32FS8 = 64,

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
    Invalid = 3,
};

enum class SurfaceTarget {
    Texture1D,
    TextureBuffer,
    Texture2D,
    Texture3D,
    Texture1DArray,
    Texture2DArray,
    TextureCubemap,
    TextureCubeArray,
};

constexpr std::array<u32, MaxPixelFormat> compression_factor_shift_table = {{
    0, // ABGR8U
    0, // ABGR8S
    0, // ABGR8UI
    0, // B5G6R5U
    0, // A2B10G10R10U
    0, // A1B5G5R5U
    0, // R8U
    0, // R8UI
    0, // RGBA16F
    0, // RGBA16U
    0, // RGBA16UI
    0, // R11FG11FB10F
    0, // RGBA32UI
    2, // DXT1
    2, // DXT23
    2, // DXT45
    2, // DXN1
    2, // DXN2UNORM
    2, // DXN2SNORM
    2, // BC7U
    2, // BC6H_UF16
    2, // BC6H_SF16
    2, // ASTC_2D_4X4
    0, // BGRA8
    0, // RGBA32F
    0, // RG32F
    0, // R32F
    0, // R16F
    0, // R16U
    0, // R16S
    0, // R16UI
    0, // R16I
    0, // RG16
    0, // RG16F
    0, // RG16UI
    0, // RG16I
    0, // RG16S
    0, // RGB32F
    0, // RGBA8_SRGB
    0, // RG8U
    0, // RG8S
    0, // RG32UI
    0, // RGBX16F
    0, // R32UI
    2, // ASTC_2D_8X8
    2, // ASTC_2D_8X5
    2, // ASTC_2D_5X4
    0, // BGRA8_SRGB
    2, // DXT1_SRGB
    2, // DXT23_SRGB
    2, // DXT45_SRGB
    2, // BC7U_SRGB
    2, // ASTC_2D_4X4_SRGB
    2, // ASTC_2D_8X8_SRGB
    2, // ASTC_2D_8X5_SRGB
    2, // ASTC_2D_5X4_SRGB
    2, // ASTC_2D_5X5
    2, // ASTC_2D_5X5_SRGB
    2, // ASTC_2D_10X8
    2, // ASTC_2D_10X8_SRGB
    0, // Z32F
    0, // Z16
    0, // Z24S8
    0, // S8Z24
    0, // Z32FS8
}};

/**
 * Gets the compression factor for the specified PixelFormat. This applies to just the
 * "compressed width" and "compressed height", not the overall compression factor of a
 * compressed image. This is used for maintaining proper surface sizes for compressed
 * texture formats.
 */
inline constexpr u32 GetCompressionFactorShift(PixelFormat format) {
    DEBUG_ASSERT(format != PixelFormat::Invalid);
    DEBUG_ASSERT(static_cast<std::size_t>(format) < compression_factor_shift_table.size());
    return compression_factor_shift_table[static_cast<std::size_t>(format)];
}

inline constexpr u32 GetCompressionFactor(PixelFormat format) {
    return 1U << GetCompressionFactorShift(format);
}

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
    1,  // RGBX16F
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

static constexpr u32 GetDefaultBlockWidth(PixelFormat format) {
    if (format == PixelFormat::Invalid)
        return 0;

    ASSERT(static_cast<std::size_t>(format) < block_width_table.size());
    return block_width_table[static_cast<std::size_t>(format)];
}

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
    1, // RGBX16F
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

static constexpr u32 GetDefaultBlockHeight(PixelFormat format) {
    if (format == PixelFormat::Invalid)
        return 0;

    ASSERT(static_cast<std::size_t>(format) < block_height_table.size());
    return block_height_table[static_cast<std::size_t>(format)];
}

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
    64,  // RGBX16F
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

static constexpr u32 GetFormatBpp(PixelFormat format) {
    if (format == PixelFormat::Invalid)
        return 0;

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

enum class SurfaceCompression {
    None,       // Not compressed
    Compressed, // Texture is compressed
    Converted,  // Texture is converted before upload or after download
    Rearranged, // Texture is swizzled before upload or after download
};

constexpr std::array<SurfaceCompression, MaxPixelFormat> compression_type_table = {{
    SurfaceCompression::None,       // ABGR8U
    SurfaceCompression::None,       // ABGR8S
    SurfaceCompression::None,       // ABGR8UI
    SurfaceCompression::None,       // B5G6R5U
    SurfaceCompression::None,       // A2B10G10R10U
    SurfaceCompression::None,       // A1B5G5R5U
    SurfaceCompression::None,       // R8U
    SurfaceCompression::None,       // R8UI
    SurfaceCompression::None,       // RGBA16F
    SurfaceCompression::None,       // RGBA16U
    SurfaceCompression::None,       // RGBA16UI
    SurfaceCompression::None,       // R11FG11FB10F
    SurfaceCompression::None,       // RGBA32UI
    SurfaceCompression::Compressed, // DXT1
    SurfaceCompression::Compressed, // DXT23
    SurfaceCompression::Compressed, // DXT45
    SurfaceCompression::Compressed, // DXN1
    SurfaceCompression::Compressed, // DXN2UNORM
    SurfaceCompression::Compressed, // DXN2SNORM
    SurfaceCompression::Compressed, // BC7U
    SurfaceCompression::Compressed, // BC6H_UF16
    SurfaceCompression::Compressed, // BC6H_SF16
    SurfaceCompression::Converted,  // ASTC_2D_4X4
    SurfaceCompression::None,       // BGRA8
    SurfaceCompression::None,       // RGBA32F
    SurfaceCompression::None,       // RG32F
    SurfaceCompression::None,       // R32F
    SurfaceCompression::None,       // R16F
    SurfaceCompression::None,       // R16U
    SurfaceCompression::None,       // R16S
    SurfaceCompression::None,       // R16UI
    SurfaceCompression::None,       // R16I
    SurfaceCompression::None,       // RG16
    SurfaceCompression::None,       // RG16F
    SurfaceCompression::None,       // RG16UI
    SurfaceCompression::None,       // RG16I
    SurfaceCompression::None,       // RG16S
    SurfaceCompression::None,       // RGB32F
    SurfaceCompression::None,       // RGBA8_SRGB
    SurfaceCompression::None,       // RG8U
    SurfaceCompression::None,       // RG8S
    SurfaceCompression::None,       // RG32UI
    SurfaceCompression::None,       // RGBX16F
    SurfaceCompression::None,       // R32UI
    SurfaceCompression::Converted,  // ASTC_2D_8X8
    SurfaceCompression::Converted,  // ASTC_2D_8X5
    SurfaceCompression::Converted,  // ASTC_2D_5X4
    SurfaceCompression::None,       // BGRA8_SRGB
    SurfaceCompression::Compressed, // DXT1_SRGB
    SurfaceCompression::Compressed, // DXT23_SRGB
    SurfaceCompression::Compressed, // DXT45_SRGB
    SurfaceCompression::Compressed, // BC7U_SRGB
    SurfaceCompression::Converted,  // ASTC_2D_4X4_SRGB
    SurfaceCompression::Converted,  // ASTC_2D_8X8_SRGB
    SurfaceCompression::Converted,  // ASTC_2D_8X5_SRGB
    SurfaceCompression::Converted,  // ASTC_2D_5X4_SRGB
    SurfaceCompression::Converted,  // ASTC_2D_5X5
    SurfaceCompression::Converted,  // ASTC_2D_5X5_SRGB
    SurfaceCompression::Converted,  // ASTC_2D_10X8
    SurfaceCompression::Converted,  // ASTC_2D_10X8_SRGB
    SurfaceCompression::None,       // Z32F
    SurfaceCompression::None,       // Z16
    SurfaceCompression::None,       // Z24S8
    SurfaceCompression::Rearranged, // S8Z24
    SurfaceCompression::None,       // Z32FS8
}};

constexpr SurfaceCompression GetFormatCompressionType(PixelFormat format) {
    if (format == PixelFormat::Invalid) {
        return SurfaceCompression::None;
    }
    DEBUG_ASSERT(static_cast<std::size_t>(format) < compression_type_table.size());
    return compression_type_table[static_cast<std::size_t>(format)];
}

SurfaceTarget SurfaceTargetFromTextureType(Tegra::Texture::TextureType texture_type);

bool SurfaceTargetIsLayered(SurfaceTarget target);

bool SurfaceTargetIsArray(SurfaceTarget target);

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

bool IsPixelFormatSRGB(PixelFormat format);

std::pair<u32, u32> GetASTCBlockSize(PixelFormat format);

/// Returns true if the specified PixelFormat is a BCn format, e.g. DXT or DXN
bool IsFormatBCn(PixelFormat format);

} // namespace VideoCore::Surface
