// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cmath>
#include <cstring>
#include "common/alignment.h"
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

/**
 * This function manages ALL the GOBs(Group of Bytes) Inside a single block.
 * Instead of going gob by gob, we map the coordinates inside a block and manage from
 * those. Block_Width is assumed to be 1.
 */
void Precise3DProcessBlock(u8* swizzled_data, u8* unswizzled_data, const bool unswizzle,
                           const u32 x_start, const u32 y_start, const u32 z_start, const u32 x_end,
                           const u32 y_end, const u32 z_end, const u32 tile_offset,
                           const u32 xy_block_size, const u32 layer_z, const u32 stride_x,
                           const u32 bytes_per_pixel, const u32 out_bytes_per_pixel) {
    std::array<u8*, 2> data_ptrs;
    u32 z_address = tile_offset;
    const u32 gob_size_x = 64;
    const u32 gob_size_y = 8;
    const u32 gob_size_z = 1;
    const u32 gob_size = gob_size_x * gob_size_y * gob_size_z;
    for (u32 z = z_start; z < z_end; z++) {
        u32 y_address = z_address;
        u32 pixel_base = layer_z * z + y_start * stride_x;
        for (u32 y = y_start; y < y_end; y++) {
            const auto& table = legacy_swizzle_table[y % gob_size_y];
            for (u32 x = x_start; x < x_end; x++) {
                const u32 swizzle_offset{y_address + table[x * bytes_per_pixel % gob_size_x]};
                const u32 pixel_index{x * out_bytes_per_pixel + pixel_base};
                data_ptrs[unswizzle] = swizzled_data + swizzle_offset;
                data_ptrs[!unswizzle] = unswizzled_data + pixel_index;
                std::memcpy(data_ptrs[0], data_ptrs[1], bytes_per_pixel);
            }
            pixel_base += stride_x;
            if ((y + 1) % gob_size_y == 0)
                y_address += gob_size;
        }
        z_address += xy_block_size;
    }
}

/**
 * This function unswizzles or swizzles a texture by mapping Linear to BlockLinear Textue.
 * The body of this function takes care of splitting the swizzled texture into blocks,
 * and managing the extents of it. Once all the parameters of a single block are obtained,
 * the function calls '3DProcessBlock' to process that particular Block.
 *
 * Documentation for the memory layout and decoding can be found at:
 *  https://envytools.readthedocs.io/en/latest/hw/memory/g80-surface.html#blocklinear-surfaces
 */
void Precise3DSwizzledData(u8* swizzled_data, u8* unswizzled_data, const bool unswizzle,
                           const u32 width, const u32 height, const u32 depth,
                           const u32 bytes_per_pixel, const u32 out_bytes_per_pixel,
                           const u32 block_height, const u32 block_depth) {
    auto div_ceil = [](const u32 x, const u32 y) { return ((x + y - 1) / y); };
    const u32 stride_x = width * out_bytes_per_pixel;
    const u32 layer_z = height * stride_x;
    const u32 gob_x_bytes = 64;
    const u32 gob_elements_x = gob_x_bytes / bytes_per_pixel;
    const u32 gob_elements_y = 8;
    const u32 gob_elements_z = 1;
    const u32 block_x_elements = gob_elements_x;
    const u32 block_y_elements = gob_elements_y * block_height;
    const u32 block_z_elements = gob_elements_z * block_depth;
    const u32 blocks_on_x = div_ceil(width, block_x_elements);
    const u32 blocks_on_y = div_ceil(height, block_y_elements);
    const u32 blocks_on_z = div_ceil(depth, block_z_elements);
    const u32 blocks = blocks_on_x * blocks_on_y * blocks_on_z;
    const u32 gob_size = gob_x_bytes * gob_elements_y * gob_elements_z;
    const u32 xy_block_size = gob_size * block_height;
    const u32 block_size = xy_block_size * block_depth;
    u32 tile_offset = 0;
    for (u32 zb = 0; zb < blocks_on_z; zb++) {
        const u32 z_start = zb * block_z_elements;
        const u32 z_end = std::min(depth, z_start + block_z_elements);
        for (u32 yb = 0; yb < blocks_on_y; yb++) {
            const u32 y_start = yb * block_y_elements;
            const u32 y_end = std::min(height, y_start + block_y_elements);
            for (u32 xb = 0; xb < blocks_on_x; xb++) {
                const u32 x_start = xb * block_x_elements;
                const u32 x_end = std::min(width, x_start + block_x_elements);
                Precise3DProcessBlock(swizzled_data, unswizzled_data, unswizzle, x_start, y_start,
                                      z_start, x_end, y_end, z_end, tile_offset, xy_block_size,
                                      layer_z, stride_x, bytes_per_pixel, out_bytes_per_pixel);
                tile_offset += block_size;
            }
        }
    }
}

/**
 * This function manages ALL the GOBs(Group of Bytes) Inside a single block.
 * Instead of going gob by gob, we map the coordinates inside a block and manage from
 * those. Block_Width is assumed to be 1.
 */
void Fast3DProcessBlock(u8* swizzled_data, u8* unswizzled_data, const bool unswizzle,
                        const u32 x_start, const u32 y_start, const u32 z_start, const u32 x_end,
                        const u32 y_end, const u32 z_end, const u32 tile_offset,
                        const u32 xy_block_size, const u32 layer_z, const u32 stride_x,
                        const u32 bytes_per_pixel, const u32 out_bytes_per_pixel) {
    std::array<u8*, 2> data_ptrs;
    u32 z_address = tile_offset;
    const u32 x_startb = x_start * bytes_per_pixel;
    const u32 x_endb = x_end * bytes_per_pixel;
    const u32 copy_size = 16;
    const u32 gob_size_x = 64;
    const u32 gob_size_y = 8;
    const u32 gob_size_z = 1;
    const u32 gob_size = gob_size_x * gob_size_y * gob_size_z;
    for (u32 z = z_start; z < z_end; z++) {
        u32 y_address = z_address;
        u32 pixel_base = layer_z * z + y_start * stride_x;
        for (u32 y = y_start; y < y_end; y++) {
            const auto& table = fast_swizzle_table[y % gob_size_y];
            for (u32 xb = x_startb; xb < x_endb; xb += copy_size) {
                const u32 swizzle_offset{y_address + table[(xb / copy_size) % 4]};
                const u32 out_x = xb * out_bytes_per_pixel / bytes_per_pixel;
                const u32 pixel_index{out_x + pixel_base};
                data_ptrs[unswizzle] = swizzled_data + swizzle_offset;
                data_ptrs[!unswizzle] = unswizzled_data + pixel_index;
                std::memcpy(data_ptrs[0], data_ptrs[1], copy_size);
            }
            pixel_base += stride_x;
            if ((y + 1) % gob_size_y == 0)
                y_address += gob_size;
        }
        z_address += xy_block_size;
    }
}

/**
 * This function unswizzles or swizzles a texture by mapping Linear to BlockLinear Textue.
 * The body of this function takes care of splitting the swizzled texture into blocks,
 * and managing the extents of it. Once all the parameters of a single block are obtained,
 * the function calls '3DProcessBlock' to process that particular Block.
 *
 * Documentation for the memory layout and decoding can be found at:
 *  https://envytools.readthedocs.io/en/latest/hw/memory/g80-surface.html#blocklinear-surfaces
 */
void Fast3DSwizzledData(u8* swizzled_data, u8* unswizzled_data, const bool unswizzle,
                        const u32 width, const u32 height, const u32 depth,
                        const u32 bytes_per_pixel, const u32 out_bytes_per_pixel,
                        const u32 block_height, const u32 block_depth) {
    auto div_ceil = [](const u32 x, const u32 y) { return ((x + y - 1) / y); };
    const u32 stride_x = width * out_bytes_per_pixel;
    const u32 layer_z = height * stride_x;
    const u32 gob_x_bytes = 64;
    const u32 gob_elements_x = gob_x_bytes / bytes_per_pixel;
    const u32 gob_elements_y = 8;
    const u32 gob_elements_z = 1;
    const u32 block_x_elements = gob_elements_x;
    const u32 block_y_elements = gob_elements_y * block_height;
    const u32 block_z_elements = gob_elements_z * block_depth;
    const u32 blocks_on_x = div_ceil(width, block_x_elements);
    const u32 blocks_on_y = div_ceil(height, block_y_elements);
    const u32 blocks_on_z = div_ceil(depth, block_z_elements);
    const u32 blocks = blocks_on_x * blocks_on_y * blocks_on_z;
    const u32 gob_size = gob_x_bytes * gob_elements_y * gob_elements_z;
    const u32 xy_block_size = gob_size * block_height;
    const u32 block_size = xy_block_size * block_depth;
    u32 tile_offset = 0;
    for (u32 zb = 0; zb < blocks_on_z; zb++) {
        const u32 z_start = zb * block_z_elements;
        const u32 z_end = std::min(depth, z_start + block_z_elements);
        for (u32 yb = 0; yb < blocks_on_y; yb++) {
            const u32 y_start = yb * block_y_elements;
            const u32 y_end = std::min(height, y_start + block_y_elements);
            for (u32 xb = 0; xb < blocks_on_x; xb++) {
                const u32 x_start = xb * block_x_elements;
                const u32 x_end = std::min(width, x_start + block_x_elements);
                Fast3DProcessBlock(swizzled_data, unswizzled_data, unswizzle, x_start, y_start,
                                   z_start, x_end, y_end, z_end, tile_offset, xy_block_size,
                                   layer_z, stride_x, bytes_per_pixel, out_bytes_per_pixel);
                tile_offset += block_size;
            }
        }
    }
}

void CopySwizzledData(u32 width, u32 height, u32 depth, u32 bytes_per_pixel,
                      u32 out_bytes_per_pixel, u8* swizzled_data, u8* unswizzled_data,
                      bool unswizzle, u32 block_height, u32 block_depth) {
    if (bytes_per_pixel % 3 != 0 && (width * bytes_per_pixel) % 16 == 0) {
        Fast3DSwizzledData(swizzled_data, unswizzled_data, unswizzle, width, height, depth,
                           bytes_per_pixel, out_bytes_per_pixel, block_height, block_depth);
    } else {
        Precise3DSwizzledData(swizzled_data, unswizzled_data, unswizzle, width, height, depth,
                              bytes_per_pixel, out_bytes_per_pixel, block_height, block_depth);
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
                                 u32 height, u32 depth, u32 block_height, u32 block_depth) {
    std::vector<u8> unswizzled_data(width * height * bytes_per_pixel);
    CopySwizzledData(width / tile_size, height / tile_size, depth, bytes_per_pixel, bytes_per_pixel,
                     Memory::GetPointer(address), unswizzled_data.data(), true, block_height,
                     block_depth);
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

std::size_t CalculateSize(bool tiled, u32 bytes_per_pixel, u32 width, u32 height, u32 depth,
                          u32 block_height, u32 block_depth) {
    if (tiled) {
        const u32 gobs_in_x = 64 / bytes_per_pixel;
        const u32 gobs_in_y = 8;
        const u32 gobs_in_z = 1;
        const u32 aligned_width = Common::AlignUp(width, gobs_in_x);
        const u32 aligned_height = Common::AlignUp(height, gobs_in_y * block_height);
        const u32 aligned_depth = Common::AlignUp(depth, gobs_in_z * block_depth);
        return aligned_width * aligned_height * aligned_depth * bytes_per_pixel;
    } else {
        return width * height * depth * bytes_per_pixel;
    }
}

} // namespace Tegra::Texture
