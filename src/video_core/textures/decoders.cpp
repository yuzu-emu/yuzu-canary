// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include "common/assert.h"
#include "video_core/textures/decoders.h"
#include "video_core/textures/texture.h"

namespace Tegra {
namespace Texture {

/**
 * Calculates the offset of an (x, y) position within a swizzled texture.
 * Taken from the Tegra X1 TRM.
 */
static u32 GetSwizzleOffset(u32 x, u32 y, u32 image_width, u32 bytes_per_pixel, u32 block_height) {
    u32 image_width_in_gobs = image_width * bytes_per_pixel / 64;
    u32 GOB_address = 0 + (y / (8 * block_height)) * 512 * block_height * image_width_in_gobs +
                      (x * bytes_per_pixel / 64) * 512 * block_height +
                      (y % (8 * block_height) / 8) * 512;
    x *= bytes_per_pixel;
    u32 address = GOB_address + ((x % 64) / 32) * 256 + ((y % 8) / 2) * 64 + ((x % 32) / 16) * 32 +
                  (y % 2) * 16 + (x % 16);

    return address;
}

static void CopySwizzledData(u32 width, u32 height, u32 bytes_per_pixel, u32 out_bytes_per_pixel,
                             u8* swizzled_data, u8* unswizzled_data, bool unswizzle,
                             u32 block_height) {
    u8* data_ptrs[2];
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            u32 swizzle_offset = GetSwizzleOffset(x, y, width, bytes_per_pixel, block_height);
            u32 pixel_index = (x + y * width) * out_bytes_per_pixel;

            data_ptrs[unswizzle] = swizzled_data + swizzle_offset;
            data_ptrs[!unswizzle] = &unswizzled_data[pixel_index];

            std::memcpy(data_ptrs[0], data_ptrs[1], bytes_per_pixel);
        }
    }
}

u32 BytesPerPixel(TextureFormat format) {
    switch (format) {
    case TextureFormat::A8R8G8B8:
    case TextureFormat::BF10GF11RF11:
        return 4;
    case TextureFormat::A1B5G5R5:
    case TextureFormat::B5G6R5:
        return 2;
    // In this case a 'pixel' actually refers to a 4x4 tile.
    case TextureFormat::BC1:
        return 8;
    case TextureFormat::BC2:
    case TextureFormat::BC3:
        return 16;
    default:
        UNIMPLEMENTED_MSG("Format not implemented");
        break;
    }
}

std::vector<u8> UnswizzleTexture(VAddr address, TextureFormat format, u32 width, u32 height,
                                 u32 block_height) {
    u8* data = Memory::GetPointer(address);
    u32 bytes_per_pixel = BytesPerPixel(format);

    static constexpr u32 DefaultBlockHeight = 16;

    std::vector<u8> unswizzled_data(width * height * bytes_per_pixel);

    switch (format) {
    case TextureFormat::BC1:
        // In the BC1 format, each 4x4 tile is swizzled instead of just individual pixel values.
        CopySwizzledData(width / 4, height / 4, bytes_per_pixel, bytes_per_pixel, data,
                         unswizzled_data.data(), true, DefaultBlockHeight);
        break;
    case TextureFormat::BC2:
        // TODO
        CopySwizzledData(width / 4, height / 4, bytes_per_pixel, bytes_per_pixel, data,
                         unswizzled_data.data(), true, DefaultBlockHeight);
        break;
    case TextureFormat::BC3:
        // TODO
        CopySwizzledData(width / 4, height / 4, bytes_per_pixel, bytes_per_pixel, data,
                         unswizzled_data.data(), true, block_height);
        break;
    case TextureFormat::A8R8G8B8:
        CopySwizzledData(width, height, bytes_per_pixel, bytes_per_pixel, data,
                         unswizzled_data.data(), true, DefaultBlockHeight);
        break;
    default:
        UNIMPLEMENTED_MSG("Format not implemented");
        break;
    }

    return unswizzled_data;
}

std::vector<u8> DecodeTexture(const std::vector<u8>& texture_data, TextureFormat format, u32 width,
                              u32 height) {
    std::vector<u8> rgba_data;

    // TODO(Subv): Implement.
    switch (format) {
    case TextureFormat::BC1:
    case TextureFormat::BC2:
    case TextureFormat::BC3:
    case TextureFormat::A8R8G8B8:
    case TextureFormat::A1B5G5R5:
    case TextureFormat::B5G6R5:
    case TextureFormat::BF10GF11RF11:
        // TODO(Subv): For the time being just forward the same data without any decoding.
        rgba_data = texture_data;
        break;
    default:
        UNIMPLEMENTED_MSG("Format not implemented");
        break;
    }

    return rgba_data;
}

} // namespace Texture
} // namespace Tegra
