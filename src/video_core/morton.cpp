// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <cstring>
#include "common/assert.h"
#include "common/common_types.h"
#include "video_core/morton.h"
#include "video_core/surface.h"
#include "video_core/textures/decoders.h"

namespace VideoCore {

using Surface::GetBytesPerPixel;
using Surface::PixelFormat;

using MortonCopyFn = void (*)(u32, u32, u32, u32, u32, u32, u8*, u8*);
using ConversionArray = std::array<MortonCopyFn, Surface::MaxPixelFormat>;

template <bool morton_to_linear, PixelFormat format>
static void MortonCopy(u32 stride, u32 block_height, u32 height, u32 block_depth, u32 depth,
                       u32 tile_width_spacing, u8* buffer, u8* addr) {
    constexpr u32 bytes_per_pixel = GetBytesPerPixel(format);

    // With the BCn formats (DXT and DXN), each 4x4 tile is swizzled instead of just individual
    // pixel values.
    constexpr u32 tile_size_x{GetDefaultBlockWidth(format)};
    constexpr u32 tile_size_y{GetDefaultBlockHeight(format)};

    if constexpr (morton_to_linear) {
        Tegra::Texture::UnswizzleTexture(buffer, addr, tile_size_x, tile_size_y, bytes_per_pixel,
                                         stride, height, depth, block_height, block_depth,
                                         tile_width_spacing);
    } else {
        Tegra::Texture::CopySwizzledData((stride + tile_size_x - 1) / tile_size_x,
                                         (height + tile_size_y - 1) / tile_size_y, depth,
                                         bytes_per_pixel, bytes_per_pixel, addr, buffer, false,
                                         block_height, block_depth, tile_width_spacing);
    }
}

static constexpr ConversionArray morton_to_linear_fns = {
    MortonCopy<true, PixelFormat::ABGR8U>,
    MortonCopy<true, PixelFormat::ABGR8S>,
    MortonCopy<true, PixelFormat::ABGR8UI>,
    MortonCopy<true, PixelFormat::B5G6R5U>,
    MortonCopy<true, PixelFormat::A2B10G10R10U>,
    MortonCopy<true, PixelFormat::A1B5G5R5U>,
    MortonCopy<true, PixelFormat::R8U>,
    MortonCopy<true, PixelFormat::R8UI>,
    MortonCopy<true, PixelFormat::RGBA16F>,
    MortonCopy<true, PixelFormat::RGBA16U>,
    MortonCopy<true, PixelFormat::RGBA16UI>,
    MortonCopy<true, PixelFormat::R11FG11FB10F>,
    MortonCopy<true, PixelFormat::RGBA32UI>,
    MortonCopy<true, PixelFormat::DXT1>,
    MortonCopy<true, PixelFormat::DXT23>,
    MortonCopy<true, PixelFormat::DXT45>,
    MortonCopy<true, PixelFormat::DXN1>,
    MortonCopy<true, PixelFormat::DXN2UNORM>,
    MortonCopy<true, PixelFormat::DXN2SNORM>,
    MortonCopy<true, PixelFormat::BC7U>,
    MortonCopy<true, PixelFormat::BC6H_UF16>,
    MortonCopy<true, PixelFormat::BC6H_SF16>,
    MortonCopy<true, PixelFormat::ASTC_2D_4X4>,
    MortonCopy<true, PixelFormat::BGRA8>,
    MortonCopy<true, PixelFormat::RGBA32F>,
    MortonCopy<true, PixelFormat::RG32F>,
    MortonCopy<true, PixelFormat::R32F>,
    MortonCopy<true, PixelFormat::R16F>,
    MortonCopy<true, PixelFormat::R16U>,
    MortonCopy<true, PixelFormat::R16S>,
    MortonCopy<true, PixelFormat::R16UI>,
    MortonCopy<true, PixelFormat::R16I>,
    MortonCopy<true, PixelFormat::RG16>,
    MortonCopy<true, PixelFormat::RG16F>,
    MortonCopy<true, PixelFormat::RG16UI>,
    MortonCopy<true, PixelFormat::RG16I>,
    MortonCopy<true, PixelFormat::RG16S>,
    MortonCopy<true, PixelFormat::RGB32F>,
    MortonCopy<true, PixelFormat::RGBA8_SRGB>,
    MortonCopy<true, PixelFormat::RG8U>,
    MortonCopy<true, PixelFormat::RG8S>,
    MortonCopy<true, PixelFormat::RG32UI>,
    MortonCopy<true, PixelFormat::R32UI>,
    MortonCopy<true, PixelFormat::ASTC_2D_8X8>,
    MortonCopy<true, PixelFormat::ASTC_2D_8X5>,
    MortonCopy<true, PixelFormat::ASTC_2D_5X4>,
    MortonCopy<true, PixelFormat::BGRA8_SRGB>,
    MortonCopy<true, PixelFormat::DXT1_SRGB>,
    MortonCopy<true, PixelFormat::DXT23_SRGB>,
    MortonCopy<true, PixelFormat::DXT45_SRGB>,
    MortonCopy<true, PixelFormat::BC7U_SRGB>,
    MortonCopy<true, PixelFormat::ASTC_2D_4X4_SRGB>,
    MortonCopy<true, PixelFormat::ASTC_2D_8X8_SRGB>,
    MortonCopy<true, PixelFormat::ASTC_2D_8X5_SRGB>,
    MortonCopy<true, PixelFormat::ASTC_2D_5X4_SRGB>,
    MortonCopy<true, PixelFormat::ASTC_2D_5X5>,
    MortonCopy<true, PixelFormat::ASTC_2D_5X5_SRGB>,
    MortonCopy<true, PixelFormat::ASTC_2D_10X8>,
    MortonCopy<true, PixelFormat::ASTC_2D_10X8_SRGB>,
    MortonCopy<true, PixelFormat::Z32F>,
    MortonCopy<true, PixelFormat::Z16>,
    MortonCopy<true, PixelFormat::Z24S8>,
    MortonCopy<true, PixelFormat::S8Z24>,
    MortonCopy<true, PixelFormat::Z32FS8>,
};

static constexpr ConversionArray linear_to_morton_fns = {
    MortonCopy<false, PixelFormat::ABGR8U>,
    MortonCopy<false, PixelFormat::ABGR8S>,
    MortonCopy<false, PixelFormat::ABGR8UI>,
    MortonCopy<false, PixelFormat::B5G6R5U>,
    MortonCopy<false, PixelFormat::A2B10G10R10U>,
    MortonCopy<false, PixelFormat::A1B5G5R5U>,
    MortonCopy<false, PixelFormat::R8U>,
    MortonCopy<false, PixelFormat::R8UI>,
    MortonCopy<false, PixelFormat::RGBA16F>,
    MortonCopy<false, PixelFormat::RGBA16U>,
    MortonCopy<false, PixelFormat::RGBA16UI>,
    MortonCopy<false, PixelFormat::R11FG11FB10F>,
    MortonCopy<false, PixelFormat::RGBA32UI>,
    MortonCopy<false, PixelFormat::DXT1>,
    MortonCopy<false, PixelFormat::DXT23>,
    MortonCopy<false, PixelFormat::DXT45>,
    MortonCopy<false, PixelFormat::DXN1>,
    MortonCopy<false, PixelFormat::DXN2UNORM>,
    MortonCopy<false, PixelFormat::DXN2SNORM>,
    MortonCopy<false, PixelFormat::BC7U>,
    MortonCopy<false, PixelFormat::BC6H_UF16>,
    MortonCopy<false, PixelFormat::BC6H_SF16>,
    // TODO(Subv): Swizzling ASTC formats are not supported
    nullptr,
    MortonCopy<false, PixelFormat::BGRA8>,
    MortonCopy<false, PixelFormat::RGBA32F>,
    MortonCopy<false, PixelFormat::RG32F>,
    MortonCopy<false, PixelFormat::R32F>,
    MortonCopy<false, PixelFormat::R16F>,
    MortonCopy<false, PixelFormat::R16U>,
    MortonCopy<false, PixelFormat::R16S>,
    MortonCopy<false, PixelFormat::R16UI>,
    MortonCopy<false, PixelFormat::R16I>,
    MortonCopy<false, PixelFormat::RG16>,
    MortonCopy<false, PixelFormat::RG16F>,
    MortonCopy<false, PixelFormat::RG16UI>,
    MortonCopy<false, PixelFormat::RG16I>,
    MortonCopy<false, PixelFormat::RG16S>,
    MortonCopy<false, PixelFormat::RGB32F>,
    MortonCopy<false, PixelFormat::RGBA8_SRGB>,
    MortonCopy<false, PixelFormat::RG8U>,
    MortonCopy<false, PixelFormat::RG8S>,
    MortonCopy<false, PixelFormat::RG32UI>,
    MortonCopy<false, PixelFormat::R32UI>,
    nullptr,
    nullptr,
    nullptr,
    MortonCopy<false, PixelFormat::BGRA8_SRGB>,
    MortonCopy<false, PixelFormat::DXT1_SRGB>,
    MortonCopy<false, PixelFormat::DXT23_SRGB>,
    MortonCopy<false, PixelFormat::DXT45_SRGB>,
    MortonCopy<false, PixelFormat::BC7U_SRGB>,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    MortonCopy<false, PixelFormat::Z32F>,
    MortonCopy<false, PixelFormat::Z16>,
    MortonCopy<false, PixelFormat::Z24S8>,
    MortonCopy<false, PixelFormat::S8Z24>,
    MortonCopy<false, PixelFormat::Z32FS8>,
};

static MortonCopyFn GetSwizzleFunction(MortonSwizzleMode mode, Surface::PixelFormat format) {
    switch (mode) {
    case MortonSwizzleMode::MortonToLinear:
        return morton_to_linear_fns[static_cast<std::size_t>(format)];
    case MortonSwizzleMode::LinearToMorton:
        return linear_to_morton_fns[static_cast<std::size_t>(format)];
    }
    UNREACHABLE();
    return morton_to_linear_fns[static_cast<std::size_t>(format)];
}

void MortonSwizzle(MortonSwizzleMode mode, Surface::PixelFormat format, u32 stride,
                   u32 block_height, u32 height, u32 block_depth, u32 depth, u32 tile_width_spacing,
                   u8* buffer, u8* addr) {
    GetSwizzleFunction(mode, format)(stride, block_height, height, block_depth, depth,
                                     tile_width_spacing, buffer, addr);
}

} // namespace VideoCore
