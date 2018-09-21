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
#include "video_core/memory_manager.h"

namespace VideoCore {
class RasterizerInterface;
}

namespace Tegra::Engines {

#define FERMI2D_REG_INDEX(field_name)                                                              \
    (offsetof(Tegra::Engines::Fermi2D::Regs, field_name) / sizeof(u32))

class Fermi2D final {
public:
    explicit Fermi2D(VideoCore::RasterizerInterface& rasterizer, MemoryManager& memory_manager);
    ~Fermi2D() = default;

    /// Write the value to the register identified by method.
    void WriteReg(u32 method, u32 value);

    struct Regs {
        static constexpr std::size_t NUM_REGS = 0x258;

        struct Surface {
            RenderTargetFormat format;
            BitField<0, 1, u32> linear;
            union {
                BitField<0, 4, u32> block_depth;
                BitField<4, 4, u32> block_height;
                BitField<8, 4, u32> block_width;
            };
            u32 depth;
            u32 layer;
            u32 pitch;
            u32 width;
            u32 height;
            u32 address_high;
            u32 address_low;

            GPUVAddr Address() const {
                return static_cast<GPUVAddr>((static_cast<GPUVAddr>(address_high) << 32) |
                                             address_low);
            }

            u32 BlockHeight() const {
                // The block height is stored in log2 format.
                return 1 << block_height;
            }
        };
        static_assert(sizeof(Surface) == 0x28, "Surface has incorrect size");

        enum class Operation : u32 {
            SrcCopyAnd = 0,
            ROPAnd = 1,
            Blend = 2,
            SrcCopy = 3,
            ROP = 4,
            SrcCopyPremult = 5,
            BlendPremult = 6,
        };

        union {
            struct {
                INSERT_PADDING_WORDS(0x80);

                Surface dst;

                INSERT_PADDING_WORDS(2);

                Surface src;

                INSERT_PADDING_WORDS(0x15);

                Operation operation;

                INSERT_PADDING_WORDS(0x9);

                // TODO(Subv): This is only a guess.
                u32 trigger;

                INSERT_PADDING_WORDS(0x1A3);
            };
            std::array<u32, NUM_REGS> reg_array;
        };
    } regs{};

    MemoryManager& memory_manager;

private:
    VideoCore::RasterizerInterface& rasterizer;

    /// Performs the copy from the source surface to the destination surface as configured in the
    /// registers.
    void HandleSurfaceCopy();
};

#define ASSERT_REG_POSITION(field_name, position)                                                  \
    static_assert(offsetof(Fermi2D::Regs, field_name) == position * 4,                             \
                  "Field " #field_name " has invalid position")

ASSERT_REG_POSITION(dst, 0x80);
ASSERT_REG_POSITION(src, 0x8C);
ASSERT_REG_POSITION(operation, 0xAB);
ASSERT_REG_POSITION(trigger, 0xB5);
#undef ASSERT_REG_POSITION

} // namespace Tegra::Engines
