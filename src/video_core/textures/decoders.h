// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <vector>
#include "common/common_types.h"
#include "video_core/textures/texture.h"

namespace Tegra::Texture {

/**
 * Unswizzles a swizzled texture without changing its format.
 */
std::vector<u8> UnswizzleTexture(VAddr address, u32 tile_size, u32 bytes_per_pixel, u32 width,
                                 u32 height, u32 depth,
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

} // namespace Tegra::Texture
