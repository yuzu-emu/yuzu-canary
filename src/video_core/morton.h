// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"
#include "video_core/surface.h"

namespace VideoCore {

enum class MortonSwizzleMode { MortonToLinear, LinearToMorton };

void MortonSwizzle(MortonSwizzleMode mode, VideoCore::Surface::PixelFormat format, u32 stride,
                   u32 block_height, u32 height, u32 block_depth, u32 depth, u32 tile_width_spacing,
                   u8* buffer, u8* addr);

void MortonCopyPixels128(MortonSwizzleMode mode, u32 width, u32 height, u32 bytes_per_pixel,
                         u32 linear_bytes_per_pixel, u8* morton_data, u8* linear_data);

} // namespace VideoCore
