// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include "common/assert.h"
#include "common/bit_field.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "video_core/gpu.h"

namespace Tegra::Engines {

#define MAXWELL_COMPUTE_REG_INDEX(field_name)                                                      \
    (offsetof(Tegra::Engines::MaxwellCompute::Regs, field_name) / sizeof(u32))

class MaxwellCompute final {
public:
    MaxwellCompute() = default;
    ~MaxwellCompute() = default;

    struct Regs {
        static constexpr std::size_t NUM_REGS = 0xCF8;

        union {
            struct {
                INSERT_PADDING_WORDS(0x281);

                union {
                    u32 compute_end;
                    BitField<0, 1, u32> unknown;
                } compute;

                INSERT_PADDING_WORDS(0xA76);
            };
            std::array<u32, NUM_REGS> reg_array;
        };
    } regs{};

    static_assert(sizeof(Regs) == Regs::NUM_REGS * sizeof(u32),
                  "MaxwellCompute Regs has wrong size");

    /// Write the value to the register identified by method.
    void CallMethod(const GPU::MethodCall& method_call);
};

#define ASSERT_REG_POSITION(field_name, position)                                                  \
    static_assert(offsetof(MaxwellCompute::Regs, field_name) == position * 4,                      \
                  "Field " #field_name " has invalid position")

ASSERT_REG_POSITION(compute, 0x281);

#undef ASSERT_REG_POSITION

} // namespace Tegra::Engines
