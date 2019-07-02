// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"

namespace VideoCommon {

struct CopyParams {
    constexpr CopyParams(u32 source_x, u32 source_y, u32 source_z, u32 dest_x, u32 dest_y,
                         u32 dest_z, u32 source_level, u32 dest_level, u32 width, u32 height,
                         u32 depth)
        : source_x{source_x}, source_y{source_y}, source_z{source_z}, dest_x{dest_x},
          dest_y{dest_y}, dest_z{dest_z}, source_level{source_level},
          dest_level{dest_level}, width{width}, height{height}, depth{depth} {}

    constexpr CopyParams(u32 width, u32 height, u32 depth, u32 level)
        : source_x{}, source_y{}, source_z{}, dest_x{}, dest_y{}, dest_z{}, source_level{level},
          dest_level{level}, width{width}, height{height}, depth{depth} {}

    u32 source_x;
    u32 source_y;
    u32 source_z;
    u32 dest_x;
    u32 dest_y;
    u32 dest_z;
    u32 source_level;
    u32 dest_level;
    u32 width;
    u32 height;
    u32 depth;
};

} // namespace VideoCommon
