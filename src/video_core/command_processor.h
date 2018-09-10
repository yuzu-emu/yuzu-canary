// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <type_traits>
#include "common/bit_field.h"
#include "common/common_types.h"
#include "video_core/memory_manager.h"

namespace Tegra {

enum class SubmissionMode : u32 {
    IncreasingOld = 0,
    Increasing = 1,
    NonIncreasingOld = 2,
    NonIncreasing = 3,
    Inline = 4,
    IncreaseOnce = 5
};

struct CommandListHeader {
    u32 entry0; // gpu_va_lo
    union {
        u32 entry1; // gpu_va_hi | (unk_0x02 << 0x08) | (size << 0x0A) | (unk_0x01 << 0x1F)
        BitField<0, 8, u32> gpu_va_hi;
        BitField<8, 2, u32> unk1;
        BitField<10, 21, u32> sz;
        BitField<31, 1, u32> unk2;
    };

    GPUVAddr Address() const {
        return (static_cast<GPUVAddr>(gpu_va_hi) << 32) | entry0;
    }
};
static_assert(sizeof(CommandListHeader) == 8, "CommandListHeader is incorrect size");

union CommandHeader {
    u32 hex;

    BitField<0, 13, u32> method;
    BitField<13, 3, u32> subchannel;

    BitField<16, 13, u32> arg_count;
    BitField<16, 13, u32> inline_data;

    BitField<29, 3, SubmissionMode> mode;
};
static_assert(std::is_standard_layout_v<CommandHeader>, "CommandHeader is not standard layout");
static_assert(sizeof(CommandHeader) == sizeof(u32), "CommandHeader has incorrect size!");

} // namespace Tegra
