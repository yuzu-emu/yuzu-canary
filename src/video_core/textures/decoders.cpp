// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cmath>
#include <cstring>
#include "common/assert.h"
#include "core/memory.h"
#include "video_core/gpu.h"
#include "video_core/textures/decoders.h"
#include "video_core/textures/texture.h"

namespace Tegra::Texture {

/**
 * This table represents the internal swizzle of a gob,
 * in format 16 bytes x 2 sector packing.
 * Calculates the offset of an (x, y) position within a swizzled texture.
 * Taken from the Tegra X1 Technical Reference Manual. pages 1187-1188
 */
template <std::size_t N, std::size_t M, u32 Align>
struct alignas(64) SwizzleTable {
    static_assert(M * Align == 64, "Swizzle Table does not align to GOB");
    constexpr SwizzleTable() {
        for (u32 y = 0; y < N; ++y) {
            for (u32 x = 0; x < M; ++x) {
                const u32 x2 = x * Align;
                values[y][x] = static_cast<u16>(((x2 % 64) / 32) * 256 + ((y % 8) / 2) * 64 +
                                                ((x2 % 32) / 16) * 32 + (y % 2) * 16 + (x2 % 16));
            }
        }
    }
    const std::array<u16, M>& operator[](std::size_t index) const {
        return values[index];
    }
    std::array<std::array<u16, M>, N> values{};
};

constexpr auto legacy_swizzle_table = SwizzleTable<8, 64, 1>();
constexpr auto fast_swizzle_table = SwizzleTable<8, 4, 16>();

static void LegacySwizzleData(u32 width, u32 height, u32 bytes_per_pixel, u32 out_bytes_per_pixel,
                              u8* swizzled_data, u8* unswizzled_data, bool unswizzle,
                              u32 block_height) {
    std::array<u8*, 2> data_ptrs;
    const std::size_t stride = width * bytes_per_pixel;
    const std::size_t gobs_in_x = 64;
    const std::size_t gobs_in_y = 8;
    const std::size_t gobs_size = gobs_in_x * gobs_in_y;
    const std::size_t image_width_in_gobs{(stride + gobs_in_x - 1) / gobs_in_x};
    for (std::size_t y = 0; y < height; ++y) {
        const std::size_t gob_y_address =
            (y / (gobs_in_y * block_height)) * gobs_size * block_height * image_width_in_gobs +
            (y % (gobs_in_y * block_height) / gobs_in_y) * gobs_size;
        const auto& table = legacy_swizzle_table[y % gobs_in_y];
        for (std::size_t x = 0; x < width; ++x) {
            const std::size_t gob_address =
                gob_y_address + (x * bytes_per_pixel / gobs_in_x) * gobs_size * block_height;
            const std::size_t x2 = x * bytes_per_pixel;
            const std::size_t swizzle_offset = gob_address + table[x2 % gobs_in_x];
            const std::size_t pixel_index = (x + y * width) * out_bytes_per_pixel;

            data_ptrs[unswizzle] = swizzled_data + swizzle_offset;
            data_ptrs[!unswizzle] = unswizzled_data + pixel_index;

            std::memcpy(data_ptrs[0], data_ptrs[1], bytes_per_pixel);
        }
    }
}

static void FastSwizzleData(u32 width, u32 height, u32 bytes_per_pixel, u32 out_bytes_per_pixel,
                            u8* swizzled_data, u8* unswizzled_data, bool unswizzle,
                            u32 block_height) {
    std::array<u8*, 2> data_ptrs;
    const std::size_t stride{width * bytes_per_pixel};
    const std::size_t gobs_in_x = 64;
    const std::size_t gobs_in_y = 8;
    const std::size_t gobs_size = gobs_in_x * gobs_in_y;
    const std::size_t image_width_in_gobs{(stride + gobs_in_x - 1) / gobs_in_x};
    const std::size_t copy_size{16};
    for (std::size_t y = 0; y < height; ++y) {
        const std::size_t initial_gob =
            (y / (gobs_in_y * block_height)) * gobs_size * block_height * image_width_in_gobs +
            (y % (gobs_in_y * block_height) / gobs_in_y) * gobs_size;
        const std::size_t pixel_base{y * width * out_bytes_per_pixel};
        const auto& table = fast_swizzle_table[y % gobs_in_y];
        for (std::size_t xb = 0; xb < stride; xb += copy_size) {
            const std::size_t gob_address{initial_gob +
                                          (xb / gobs_in_x) * gobs_size * block_height};
            const std::size_t swizzle_offset{gob_address + table[(xb / 16) % 4]};
            const std::size_t out_x = xb * out_bytes_per_pixel / bytes_per_pixel;
            const std::size_t pixel_index{out_x + pixel_base};
            data_ptrs[unswizzle] = swizzled_data + swizzle_offset;
            data_ptrs[!unswizzle] = unswizzled_data + pixel_index;
            std::memcpy(data_ptrs[0], data_ptrs[1], copy_size);
        }
    }
}

void CopySwizzledData(u32 width, u32 height, u32 bytes_per_pixel, u32 out_bytes_per_pixel,
                      u8* swizzled_data, u8* unswizzled_data, bool unswizzle, u32 block_height) {
    if (bytes_per_pixel % 3 != 0 && (width * bytes_per_pixel) % 16 == 0) {
        FastSwizzleData(width, height, bytes_per_pixel, out_bytes_per_pixel, swizzled_data,
                        unswizzled_data, unswizzle, block_height);
    } else {
        LegacySwizzleData(width, height, bytes_per_pixel, out_bytes_per_pixel, swizzled_data,
                          unswizzled_data, unswizzle, block_height);
    }
}

u32 BytesPerPixel(TextureFormat format) {
    switch (format) {
    case TextureFormat::DXT1:
    case TextureFormat::DXN1:
        // In this case a 'pixel' actually refers to a 4x4 tile.
        return 8;
    case TextureFormat::DXT23:
    case TextureFormat::DXT45:
    case TextureFormat::DXN2:
    case TextureFormat::BC7U:
    case TextureFormat::BC6H_UF16:
    case TextureFormat::BC6H_SF16:
        // In this case a 'pixel' actually refers to a 4x4 tile.
        return 16;
    case TextureFormat::R32_G32_B32:
        return 12;
    case TextureFormat::ASTC_2D_4X4:
    case TextureFormat::ASTC_2D_8X8:
    case TextureFormat::A8R8G8B8:
    case TextureFormat::A2B10G10R10:
    case TextureFormat::BF10GF11RF11:
    case TextureFormat::R32:
    case TextureFormat::R16_G16:
        return 4;
    case TextureFormat::A1B5G5R5:
    case TextureFormat::B5G6R5:
    case TextureFormat::G8R8:
    case TextureFormat::R16:
        return 2;
    case TextureFormat::R8:
        return 1;
    case TextureFormat::R16_G16_B16_A16:
        return 8;
    case TextureFormat::R32_G32_B32_A32:
        return 16;
    case TextureFormat::R32_G32:
        return 8;
    default:
        UNIMPLEMENTED_MSG("Format not implemented");
        break;
    }
}

std::vector<u8> UnswizzleTexture(VAddr address, u32 tile_size, u32 bytes_per_pixel, u32 width,
                                 u32 height, u32 block_height) {
    std::vector<u8> unswizzled_data(width * height * bytes_per_pixel);
    CopySwizzledData(width / tile_size, height / tile_size, bytes_per_pixel, bytes_per_pixel,
                     Memory::GetPointer(address), unswizzled_data.data(), true, block_height);
    return unswizzled_data;
}

std::vector<u8> DecodeTexture(const std::vector<u8>& texture_data, TextureFormat format, u32 width,
                              u32 height) {
    std::vector<u8> rgba_data;

    // TODO(Subv): Implement.
    switch (format) {
    case TextureFormat::DXT1:
    case TextureFormat::DXT23:
    case TextureFormat::DXT45:
    case TextureFormat::DXN1:
    case TextureFormat::DXN2:
    case TextureFormat::BC7U:
    case TextureFormat::BC6H_UF16:
    case TextureFormat::BC6H_SF16:
    case TextureFormat::ASTC_2D_4X4:
    case TextureFormat::ASTC_2D_8X8:
    case TextureFormat::A8R8G8B8:
    case TextureFormat::A2B10G10R10:
    case TextureFormat::A1B5G5R5:
    case TextureFormat::B5G6R5:
    case TextureFormat::R8:
    case TextureFormat::G8R8:
    case TextureFormat::BF10GF11RF11:
    case TextureFormat::R32_G32_B32_A32:
    case TextureFormat::R32_G32:
    case TextureFormat::R32:
    case TextureFormat::R16:
    case TextureFormat::R16_G16:
    case TextureFormat::R32_G32_B32:
        // TODO(Subv): For the time being just forward the same data without any decoding.
        rgba_data = texture_data;
        break;
    default:
        UNIMPLEMENTED_MSG("Format not implemented");
        break;
    }

    return rgba_data;
}

} // namespace Tegra::Texture
