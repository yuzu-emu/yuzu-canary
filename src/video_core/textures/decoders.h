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
                                 u32 height, u32 block_height = TICEntry::DefaultBlockHeight);

/**
 * Unswizzles a swizzled depth texture without changing its format.
 */
std::vector<u8> UnswizzleDepthTexture(VAddr address, DepthFormat format, u32 width, u32 height,
                                      u32 block_height = TICEntry::DefaultBlockHeight);

/// Copies texture data from a buffer and performs swizzling/unswizzling as necessary.
void CopySwizzledData(u32 width, u32 height, u32 bytes_per_pixel, u32 out_bytes_per_pixel,
                      u8* swizzled_data, u8* unswizzled_data, bool unswizzle, u32 block_height);

/// Copies an untiled subrectangle into a tiled surface.
void SwizzleSubrect(u32 subrect_width, u32 subrect_height, u32 source_pitch, u32 swizzled_width,
                    u32 bytes_per_pixel, VAddr swizzled_data, VAddr unswizzled_data,
                    u32 block_height);

/**
 * Decodes an unswizzled texture into a A8R8G8B8 texture.
 */
std::vector<u8> DecodeTexture(const std::vector<u8>& texture_data, TextureFormat format, u32 width,
                              u32 height);

} // namespace Tegra::Texture
