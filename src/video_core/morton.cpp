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
    const u32 tile_size_x{GetDefaultBlockWidth(format)};
    const u32 tile_size_y{GetDefaultBlockHeight(format)};

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

static u32 MortonInterleave128(u32 x, u32 y) {
    // 128x128 Z-Order coordinate from 2D coordinates
    static constexpr u32 xlut[] = {
        0x0000, 0x0001, 0x0002, 0x0003, 0x0008, 0x0009, 0x000a, 0x000b, 0x0040, 0x0041, 0x0042,
        0x0043, 0x0048, 0x0049, 0x004a, 0x004b, 0x0800, 0x0801, 0x0802, 0x0803, 0x0808, 0x0809,
        0x080a, 0x080b, 0x0840, 0x0841, 0x0842, 0x0843, 0x0848, 0x0849, 0x084a, 0x084b, 0x1000,
        0x1001, 0x1002, 0x1003, 0x1008, 0x1009, 0x100a, 0x100b, 0x1040, 0x1041, 0x1042, 0x1043,
        0x1048, 0x1049, 0x104a, 0x104b, 0x1800, 0x1801, 0x1802, 0x1803, 0x1808, 0x1809, 0x180a,
        0x180b, 0x1840, 0x1841, 0x1842, 0x1843, 0x1848, 0x1849, 0x184a, 0x184b, 0x2000, 0x2001,
        0x2002, 0x2003, 0x2008, 0x2009, 0x200a, 0x200b, 0x2040, 0x2041, 0x2042, 0x2043, 0x2048,
        0x2049, 0x204a, 0x204b, 0x2800, 0x2801, 0x2802, 0x2803, 0x2808, 0x2809, 0x280a, 0x280b,
        0x2840, 0x2841, 0x2842, 0x2843, 0x2848, 0x2849, 0x284a, 0x284b, 0x3000, 0x3001, 0x3002,
        0x3003, 0x3008, 0x3009, 0x300a, 0x300b, 0x3040, 0x3041, 0x3042, 0x3043, 0x3048, 0x3049,
        0x304a, 0x304b, 0x3800, 0x3801, 0x3802, 0x3803, 0x3808, 0x3809, 0x380a, 0x380b, 0x3840,
        0x3841, 0x3842, 0x3843, 0x3848, 0x3849, 0x384a, 0x384b, 0x0000, 0x0001, 0x0002, 0x0003,
        0x0008, 0x0009, 0x000a, 0x000b, 0x0040, 0x0041, 0x0042, 0x0043, 0x0048, 0x0049, 0x004a,
        0x004b, 0x0800, 0x0801, 0x0802, 0x0803, 0x0808, 0x0809, 0x080a, 0x080b, 0x0840, 0x0841,
        0x0842, 0x0843, 0x0848, 0x0849, 0x084a, 0x084b, 0x1000, 0x1001, 0x1002, 0x1003, 0x1008,
        0x1009, 0x100a, 0x100b, 0x1040, 0x1041, 0x1042, 0x1043, 0x1048, 0x1049, 0x104a, 0x104b,
        0x1800, 0x1801, 0x1802, 0x1803, 0x1808, 0x1809, 0x180a, 0x180b, 0x1840, 0x1841, 0x1842,
        0x1843, 0x1848, 0x1849, 0x184a, 0x184b, 0x2000, 0x2001, 0x2002, 0x2003, 0x2008, 0x2009,
        0x200a, 0x200b, 0x2040, 0x2041, 0x2042, 0x2043, 0x2048, 0x2049, 0x204a, 0x204b, 0x2800,
        0x2801, 0x2802, 0x2803, 0x2808, 0x2809, 0x280a, 0x280b, 0x2840, 0x2841, 0x2842, 0x2843,
        0x2848, 0x2849, 0x284a, 0x284b, 0x3000, 0x3001, 0x3002, 0x3003, 0x3008, 0x3009, 0x300a,
        0x300b, 0x3040, 0x3041, 0x3042, 0x3043, 0x3048, 0x3049, 0x304a, 0x304b, 0x3800, 0x3801,
        0x3802, 0x3803, 0x3808, 0x3809, 0x380a, 0x380b, 0x3840, 0x3841, 0x3842, 0x3843, 0x3848,
        0x3849, 0x384a, 0x384b, 0x0000, 0x0001, 0x0002, 0x0003, 0x0008, 0x0009, 0x000a, 0x000b,
        0x0040, 0x0041, 0x0042, 0x0043, 0x0048, 0x0049, 0x004a, 0x004b, 0x0800, 0x0801, 0x0802,
        0x0803, 0x0808, 0x0809, 0x080a, 0x080b, 0x0840, 0x0841, 0x0842, 0x0843, 0x0848, 0x0849,
        0x084a, 0x084b, 0x1000, 0x1001, 0x1002, 0x1003, 0x1008, 0x1009, 0x100a, 0x100b, 0x1040,
        0x1041, 0x1042, 0x1043, 0x1048, 0x1049, 0x104a, 0x104b, 0x1800, 0x1801, 0x1802, 0x1803,
        0x1808, 0x1809, 0x180a, 0x180b, 0x1840, 0x1841, 0x1842, 0x1843, 0x1848, 0x1849, 0x184a,
        0x184b, 0x2000, 0x2001, 0x2002, 0x2003, 0x2008, 0x2009, 0x200a, 0x200b, 0x2040, 0x2041,
        0x2042, 0x2043, 0x2048, 0x2049, 0x204a, 0x204b, 0x2800, 0x2801, 0x2802, 0x2803, 0x2808,
        0x2809, 0x280a, 0x280b, 0x2840, 0x2841, 0x2842, 0x2843, 0x2848, 0x2849, 0x284a, 0x284b,
        0x3000, 0x3001, 0x3002, 0x3003, 0x3008, 0x3009, 0x300a, 0x300b, 0x3040, 0x3041, 0x3042,
        0x3043, 0x3048, 0x3049, 0x304a, 0x304b, 0x3800, 0x3801, 0x3802, 0x3803, 0x3808, 0x3809,
        0x380a, 0x380b, 0x3840, 0x3841, 0x3842, 0x3843, 0x3848, 0x3849, 0x384a, 0x384b,
    };
    static constexpr u32 ylut[] = {
        0x0000, 0x0004, 0x0010, 0x0014, 0x0020, 0x0024, 0x0030, 0x0034, 0x0080, 0x0084, 0x0090,
        0x0094, 0x00a0, 0x00a4, 0x00b0, 0x00b4, 0x0100, 0x0104, 0x0110, 0x0114, 0x0120, 0x0124,
        0x0130, 0x0134, 0x0180, 0x0184, 0x0190, 0x0194, 0x01a0, 0x01a4, 0x01b0, 0x01b4, 0x0200,
        0x0204, 0x0210, 0x0214, 0x0220, 0x0224, 0x0230, 0x0234, 0x0280, 0x0284, 0x0290, 0x0294,
        0x02a0, 0x02a4, 0x02b0, 0x02b4, 0x0300, 0x0304, 0x0310, 0x0314, 0x0320, 0x0324, 0x0330,
        0x0334, 0x0380, 0x0384, 0x0390, 0x0394, 0x03a0, 0x03a4, 0x03b0, 0x03b4, 0x0400, 0x0404,
        0x0410, 0x0414, 0x0420, 0x0424, 0x0430, 0x0434, 0x0480, 0x0484, 0x0490, 0x0494, 0x04a0,
        0x04a4, 0x04b0, 0x04b4, 0x0500, 0x0504, 0x0510, 0x0514, 0x0520, 0x0524, 0x0530, 0x0534,
        0x0580, 0x0584, 0x0590, 0x0594, 0x05a0, 0x05a4, 0x05b0, 0x05b4, 0x0600, 0x0604, 0x0610,
        0x0614, 0x0620, 0x0624, 0x0630, 0x0634, 0x0680, 0x0684, 0x0690, 0x0694, 0x06a0, 0x06a4,
        0x06b0, 0x06b4, 0x0700, 0x0704, 0x0710, 0x0714, 0x0720, 0x0724, 0x0730, 0x0734, 0x0780,
        0x0784, 0x0790, 0x0794, 0x07a0, 0x07a4, 0x07b0, 0x07b4, 0x0000, 0x0004, 0x0010, 0x0014,
        0x0020, 0x0024, 0x0030, 0x0034, 0x0080, 0x0084, 0x0090, 0x0094, 0x00a0, 0x00a4, 0x00b0,
        0x00b4, 0x0100, 0x0104, 0x0110, 0x0114, 0x0120, 0x0124, 0x0130, 0x0134, 0x0180, 0x0184,
        0x0190, 0x0194, 0x01a0, 0x01a4, 0x01b0, 0x01b4, 0x0200, 0x0204, 0x0210, 0x0214, 0x0220,
        0x0224, 0x0230, 0x0234, 0x0280, 0x0284, 0x0290, 0x0294, 0x02a0, 0x02a4, 0x02b0, 0x02b4,
        0x0300, 0x0304, 0x0310, 0x0314, 0x0320, 0x0324, 0x0330, 0x0334, 0x0380, 0x0384, 0x0390,
        0x0394, 0x03a0, 0x03a4, 0x03b0, 0x03b4, 0x0400, 0x0404, 0x0410, 0x0414, 0x0420, 0x0424,
        0x0430, 0x0434, 0x0480, 0x0484, 0x0490, 0x0494, 0x04a0, 0x04a4, 0x04b0, 0x04b4, 0x0500,
        0x0504, 0x0510, 0x0514, 0x0520, 0x0524, 0x0530, 0x0534, 0x0580, 0x0584, 0x0590, 0x0594,
        0x05a0, 0x05a4, 0x05b0, 0x05b4, 0x0600, 0x0604, 0x0610, 0x0614, 0x0620, 0x0624, 0x0630,
        0x0634, 0x0680, 0x0684, 0x0690, 0x0694, 0x06a0, 0x06a4, 0x06b0, 0x06b4, 0x0700, 0x0704,
        0x0710, 0x0714, 0x0720, 0x0724, 0x0730, 0x0734, 0x0780, 0x0784, 0x0790, 0x0794, 0x07a0,
        0x07a4, 0x07b0, 0x07b4, 0x0000, 0x0004, 0x0010, 0x0014, 0x0020, 0x0024, 0x0030, 0x0034,
        0x0080, 0x0084, 0x0090, 0x0094, 0x00a0, 0x00a4, 0x00b0, 0x00b4, 0x0100, 0x0104, 0x0110,
        0x0114, 0x0120, 0x0124, 0x0130, 0x0134, 0x0180, 0x0184, 0x0190, 0x0194, 0x01a0, 0x01a4,
        0x01b0, 0x01b4, 0x0200, 0x0204, 0x0210, 0x0214, 0x0220, 0x0224, 0x0230, 0x0234, 0x0280,
        0x0284, 0x0290, 0x0294, 0x02a0, 0x02a4, 0x02b0, 0x02b4, 0x0300, 0x0304, 0x0310, 0x0314,
        0x0320, 0x0324, 0x0330, 0x0334, 0x0380, 0x0384, 0x0390, 0x0394, 0x03a0, 0x03a4, 0x03b0,
        0x03b4, 0x0400, 0x0404, 0x0410, 0x0414, 0x0420, 0x0424, 0x0430, 0x0434, 0x0480, 0x0484,
        0x0490, 0x0494, 0x04a0, 0x04a4, 0x04b0, 0x04b4, 0x0500, 0x0504, 0x0510, 0x0514, 0x0520,
        0x0524, 0x0530, 0x0534, 0x0580, 0x0584, 0x0590, 0x0594, 0x05a0, 0x05a4, 0x05b0, 0x05b4,
        0x0600, 0x0604, 0x0610, 0x0614, 0x0620, 0x0624, 0x0630, 0x0634, 0x0680, 0x0684, 0x0690,
        0x0694, 0x06a0, 0x06a4, 0x06b0, 0x06b4, 0x0700, 0x0704, 0x0710, 0x0714, 0x0720, 0x0724,
        0x0730, 0x0734, 0x0780, 0x0784, 0x0790, 0x0794, 0x07a0, 0x07a4, 0x07b0, 0x07b4,
    };
    return xlut[x % 128] + ylut[y % 128];
}

static u32 GetMortonOffset128(u32 x, u32 y, u32 bytes_per_pixel) {
    // Calculates the offset of the position of the pixel in Morton order
    // Framebuffer images are split into 128x128 tiles.

    constexpr u32 block_height = 128;
    const u32 coarse_x = x & ~127;

    const u32 i = MortonInterleave128(x, y);

    const u32 offset = coarse_x * block_height;

    return (i + offset) * bytes_per_pixel;
}

void MortonSwizzle(MortonSwizzleMode mode, Surface::PixelFormat format, u32 stride,
                   u32 block_height, u32 height, u32 block_depth, u32 depth, u32 tile_width_spacing,
                   u8* buffer, u8* addr) {
    GetSwizzleFunction(mode, format)(stride, block_height, height, block_depth, depth,
                                     tile_width_spacing, buffer, addr);
}

void MortonCopyPixels128(MortonSwizzleMode mode, u32 width, u32 height, u32 bytes_per_pixel,
                         u32 linear_bytes_per_pixel, u8* morton_data, u8* linear_data) {
    const bool morton_to_linear = mode == MortonSwizzleMode::MortonToLinear;
    u8* data_ptrs[2];
    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
            const u32 coarse_y = y & ~127;
            const u32 morton_offset =
                GetMortonOffset128(x, y, bytes_per_pixel) + coarse_y * width * bytes_per_pixel;
            const u32 linear_pixel_index = (x + y * width) * linear_bytes_per_pixel;

            data_ptrs[morton_to_linear ? 1 : 0] = morton_data + morton_offset;
            data_ptrs[morton_to_linear ? 0 : 1] = &linear_data[linear_pixel_index];

            std::memcpy(data_ptrs[0], data_ptrs[1], bytes_per_pixel);
        }
    }
}

} // namespace VideoCore
