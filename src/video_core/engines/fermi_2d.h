// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include "common/bit_field.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/math_util.h"
#include "video_core/gpu.h"

namespace Tegra {
class MemoryManager;
}

namespace VideoCore {
class RasterizerInterface;
}

namespace Tegra::Engines {

/**
 * This Engine is known as G80_2D. Documentation can be found in:
 * https://github.com/envytools/envytools/blob/master/rnndb/graph/g80_2d.xml
 * https://cgit.freedesktop.org/mesa/mesa/tree/src/gallium/drivers/nouveau/nv50/nv50_2d.xml.h
 */

#define FERMI2D_REG_INDEX(field_name)                                                              \
    (offsetof(Tegra::Engines::Fermi2D::Regs, field_name) / sizeof(u32))

class Fermi2D final {
public:
    explicit Fermi2D(VideoCore::RasterizerInterface& rasterizer);
    ~Fermi2D() = default;

    /// Write the value to the register identified by method.
    void CallMethod(const GPU::MethodCall& method_call);

    enum class Origin : u32 {
        Center = 0,
        Corner = 1,
    };

    enum class Filter : u32 {
        PointSample = 0, // Nearest
        Linear = 1,
    };

    enum class Operation : u32 {
        SrcCopyAnd = 0,
        ROPAnd = 1,
        Blend = 2,
        SrcCopy = 3,
        ROP = 4,
        SrcCopyPremult = 5,
        BlendPremult = 6,
    };

    struct Regs {
        static constexpr std::size_t NUM_REGS = 0x258;

        struct Surface {
            RenderTargetFormat format;
            BitField<0, 1, u32> linear;
            union {
                BitField<0, 4, u32> block_width;
                BitField<4, 4, u32> block_height;
                BitField<8, 4, u32> block_depth;
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

            u32 BlockWidth() const {
                return block_width.Value();
            }

            u32 BlockHeight() const {
                return block_height.Value();
            }

            u32 BlockDepth() const {
                return block_depth.Value();
            }
        };
        static_assert(sizeof(Surface) == 0x28, "Surface has incorrect size");

        union {
            struct {
                INSERT_PADDING_WORDS(0x80);

                Surface dst;

                INSERT_PADDING_WORDS(2);

                Surface src;

                INSERT_PADDING_WORDS(0x15);

                Operation operation;

                INSERT_PADDING_WORDS(0x177);

                union {
                    u32 raw;
                    BitField<0, 1, Origin> origin;
                    BitField<4, 1, Filter> filter;
                } blit_control;

                INSERT_PADDING_WORDS(0x8);

                u32 blit_dst_x;
                u32 blit_dst_y;
                u32 blit_dst_width;
                u32 blit_dst_height;
                u64 blit_du_dx;
                u64 blit_dv_dy;
                u64 blit_src_x;
                u64 blit_src_y;

                INSERT_PADDING_WORDS(0x21);
            };
            std::array<u32, NUM_REGS> reg_array;
        };
    } regs{};

    struct Config {
        Operation operation;
        Filter filter;
        Common::Rectangle<u32> src_rect;
        Common::Rectangle<u32> dst_rect;
    };

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
ASSERT_REG_POSITION(blit_control, 0x223);
ASSERT_REG_POSITION(blit_dst_x, 0x22c);
ASSERT_REG_POSITION(blit_dst_y, 0x22d);
ASSERT_REG_POSITION(blit_dst_width, 0x22e);
ASSERT_REG_POSITION(blit_dst_height, 0x22f);
ASSERT_REG_POSITION(blit_du_dx, 0x230);
ASSERT_REG_POSITION(blit_dv_dy, 0x232);
ASSERT_REG_POSITION(blit_src_x, 0x234);
ASSERT_REG_POSITION(blit_src_y, 0x236);

#undef ASSERT_REG_POSITION

} // namespace Tegra::Engines
