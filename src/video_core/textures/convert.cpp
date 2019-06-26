// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cstring>
#include <tuple>
#include <vector>

#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "video_core/surface.h"
#include "video_core/textures/astc.h"
#include "video_core/textures/convert.h"

namespace Tegra::Texture {

using VideoCore::Surface::PixelFormat;

template <bool reverse>
void SwapS8Z24ToZ24S8(u8* data, u32 width, u32 height) {
    union S8Z24 {
        BitField<0, 24, u32> z24;
        BitField<24, 8, u32> s8;
    };
    static_assert(sizeof(S8Z24) == 4, "S8Z24 is incorrect size");

    union Z24S8 {
        BitField<0, 8, u32> s8;
        BitField<8, 24, u32> z24;
    };
    static_assert(sizeof(Z24S8) == 4, "Z24S8 is incorrect size");

    S8Z24 s8z24_pixel{};
    Z24S8 z24s8_pixel{};
    constexpr auto bpp{
        VideoCore::Surface::GetBytesPerPixel(VideoCore::Surface::PixelFormat::S8Z24)};
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const std::size_t offset{bpp * (y * width + x)};
            if constexpr (reverse) {
                std::memcpy(&z24s8_pixel, &data[offset], sizeof(Z24S8));
                s8z24_pixel.s8.Assign(z24s8_pixel.s8);
                s8z24_pixel.z24.Assign(z24s8_pixel.z24);
                std::memcpy(&data[offset], &s8z24_pixel, sizeof(S8Z24));
            } else {
                std::memcpy(&s8z24_pixel, &data[offset], sizeof(S8Z24));
                z24s8_pixel.s8.Assign(s8z24_pixel.s8);
                z24s8_pixel.z24.Assign(s8z24_pixel.z24);
                std::memcpy(&data[offset], &z24s8_pixel, sizeof(Z24S8));
            }
        }
    }
}

static void ConvertS8Z24ToZ24S8(u8* data, u32 width, u32 height) {
    SwapS8Z24ToZ24S8<false>(data, width, height);
}

static void ConvertZ24S8ToS8Z24(u8* data, u32 width, u32 height) {
    SwapS8Z24ToZ24S8<true>(data, width, height);
}

void ConvertFromGuestToHost(u8* in_data, u8* out_data, PixelFormat pixel_format, u32 width,
                            u32 height, u32 depth, bool convert_astc, bool convert_s8z24) {
    if (convert_astc && IsPixelFormatASTC(pixel_format)) {
        // Convert ASTC pixel formats to RGBA8, as most desktop GPUs do not support ASTC.
        u32 block_width{};
        u32 block_height{};
        std::tie(block_width, block_height) = GetASTCBlockSize(pixel_format);
        const std::vector<u8> rgba8_data = Tegra::Texture::ASTC::Decompress(
            in_data, width, height, depth, block_width, block_height);
        std::copy(rgba8_data.begin(), rgba8_data.end(), out_data);

    } else if (convert_s8z24 && pixel_format == PixelFormat::S8Z24) {
        Tegra::Texture::ConvertS8Z24ToZ24S8(in_data, width, height);
    }
}

void ConvertFromHostToGuest(u8* data, PixelFormat pixel_format, u32 width, u32 height, u32 depth,
                            bool convert_astc, bool convert_s8z24) {
    if (convert_astc && IsPixelFormatASTC(pixel_format)) {
        LOG_CRITICAL(HW_GPU, "Conversion of format {} after texture flushing is not implemented",
                     static_cast<u32>(pixel_format));
        UNREACHABLE();

    } else if (convert_s8z24 && pixel_format == PixelFormat::S8Z24) {
        Tegra::Texture::ConvertZ24S8ToS8Z24(data, width, height);
    }
}

} // namespace Tegra::Texture
