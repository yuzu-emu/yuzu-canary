// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <vector>
#include "common/common_types.h"
#include "video_core/textures/texture.h"

namespace Tegra {
namespace Texture {

/**
 * Unswizzles a swizzled texture without changing its format.
 */
std::vector<u8> UnswizzleTexture(VAddr address, TextureFormat format, u32 width, u32 height,
                                 u32 block_height);

/**
 * Decodes an unswizzled texture into a A8R8G8B8 texture.
 */
std::vector<u8> DecodeTexture(const std::vector<u8>& texture_data, TextureFormat format, u32 width,
                              u32 height);

} // namespace Texture
} // namespace Tegra
