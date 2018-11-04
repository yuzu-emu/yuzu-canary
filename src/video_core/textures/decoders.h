// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <vector>
#include "common/common_types.h"
#include "video_core/textures/texture.h"

namespace Tegra::Texture {

// GOBSize constant. Calculated by 64 bytes in x multiplied by 8 y coords, represents
// an small rect of (64/bytes_per_pixel)X8.
inline std::size_t GetGOBSize() {
    return 512;
}

/**
 * Unswizzles a swizzled texture without changing its format.
 */
std::vector<u8> UnswizzleTexture(VAddr address, u32 tile_size_x, u32 tile_size_y,
                                 u32 bytes_per_pixel, u32 width, u32 height, u32 depth,
                                 u32 block_height = TICEntry::DefaultBlockHeight,
                                 u32 block_depth = TICEntry::DefaultBlockHeight);

/// Copies texture data from a buffer and performs swizzling/unswizzling as necessary.
void CopySwizzledData(u32 width, u32 height, u32 depth, u32 bytes_per_pixel,
                      u32 out_bytes_per_pixel, u8* swizzled_data, u8* unswizzled_data,
                      bool unswizzle, u32 block_height, u32 block_depth);

/**
 * Decodes an unswizzled texture into a A8R8G8B8 texture.
 */
std::vector<u8> DecodeTexture(const std::vector<u8>& texture_data, TextureFormat format, u32 width,
                              u32 height);

/**
 * This function calculates the correct size of a texture depending if it's tiled or not.
 */
std::size_t CalculateSize(bool tiled, u32 bytes_per_pixel, u32 width, u32 height, u32 depth,
                          u32 block_height, u32 block_depth);

/// Copies an untiled subrectangle into a tiled surface.
void SwizzleSubrect(u32 subrect_width, u32 subrect_height, u32 source_pitch, u32 swizzled_width,
                    u32 bytes_per_pixel, VAddr swizzled_data, VAddr unswizzled_data,
                    u32 block_height);
/// Copies a tiled subrectangle into a linear surface.
void UnswizzleSubrect(u32 subrect_width, u32 subrect_height, u32 dest_pitch, u32 swizzled_width,
                      u32 bytes_per_pixel, VAddr swizzled_data, VAddr unswizzled_data,
                      u32 block_height, u32 offset_x, u32 offset_y);

} // namespace Tegra::Texture
