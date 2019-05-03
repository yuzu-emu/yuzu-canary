// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <vector>
#include "common/bit_field.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "video_core/gpu.h"

namespace Core {
class System;
}

namespace Tegra {
class MemoryManager;
}

namespace VideoCore {
class RasterizerInterface;
}

namespace Tegra::Engines {

/**
 * This Engine is known as GK104_Copy. Documentation can be found in:
 * https://github.com/envytools/envytools/blob/master/rnndb/fifo/gk104_copy.xml
 */

class MaxwellDMA final {
public:
    explicit MaxwellDMA(Core::System& system, VideoCore::RasterizerInterface& rasterizer,
                        MemoryManager& memory_manager);
    ~MaxwellDMA() = default;

    /// Write the value to the register identified by method.
    void CallMethod(const GPU::MethodCall& method_call);

    struct Regs {
        static constexpr std::size_t NUM_REGS = 0x1D6;

        struct Parameters {
            union {
                BitField<0, 4, u32> block_depth;
                BitField<4, 4, u32> block_height;
                BitField<8, 4, u32> block_width;
            };
            u32 size_x;
            u32 size_y;
            u32 size_z;
            u32 pos_z;
            union {
                BitField<0, 16, u32> pos_x;
                BitField<16, 16, u32> pos_y;
            };

            u32 BlockHeight() const {
                return 1 << block_height;
            }

            u32 BlockDepth() const {
                return 1 << block_depth;
            }
        };

        static_assert(sizeof(Parameters) == 24, "Parameters has wrong size");

        enum class ComponentMode : u32 {
            Src0 = 0,
            Src1 = 1,
            Src2 = 2,
            Src3 = 3,
            Const0 = 4,
            Const1 = 5,
            Zero = 6,
        };

        enum class CopyMode : u32 {
            None = 0,
            Unk1 = 1,
            Unk2 = 2,
        };

        enum class QueryMode : u32 {
            None = 0,
            Short = 1,
            Long = 2,
        };

        enum class QueryIntr : u32 {
            None = 0,
            Block = 1,
            NonBlock = 2,
        };

        union {
            struct {
                INSERT_PADDING_WORDS(0xC0);

                struct {
                    union {
                        BitField<0, 2, CopyMode> copy_mode;
                        BitField<2, 1, u32> flush;

                        BitField<3, 2, QueryMode> query_mode;
                        BitField<5, 2, QueryIntr> query_intr;

                        BitField<7, 1, u32> is_src_linear;
                        BitField<8, 1, u32> is_dst_linear;

                        BitField<9, 1, u32> enable_2d;
                        BitField<10, 1, u32> enable_swizzle;
                    };
                } exec;

                INSERT_PADDING_WORDS(0x3F);

                struct {
                    u32 address_high;
                    u32 address_low;

                    GPUVAddr Address() const {
                        return static_cast<GPUVAddr>((static_cast<GPUVAddr>(address_high) << 32) |
                                                     address_low);
                    }
                } src_address;

                struct {
                    u32 address_high;
                    u32 address_low;

                    GPUVAddr Address() const {
                        return static_cast<GPUVAddr>((static_cast<GPUVAddr>(address_high) << 32) |
                                                     address_low);
                    }
                } dst_address;

                u32 src_pitch;
                u32 dst_pitch;
                u32 x_count;
                u32 y_count;

                INSERT_PADDING_WORDS(0xB8);

                u32 const0;
                u32 const1;
                union {
                    BitField<0, 4, ComponentMode> component0;
                    BitField<4, 4, ComponentMode> component1;
                    BitField<8, 4, ComponentMode> component2;
                    BitField<12, 4, ComponentMode> component3;
                    BitField<16, 2, u32> component_size;
                    BitField<20, 3, u32> src_num_components;
                    BitField<24, 3, u32> dst_num_components;

                    u32 SrcBytePerPixel() const {
                        return src_num_components.Value() * component_size.Value();
                    }
                    u32 DstBytePerPixel() const {
                        return dst_num_components.Value() * component_size.Value();
                    }
                } swizzle_config;

                Parameters dst_params;

                INSERT_PADDING_WORDS(1);

                Parameters src_params;

                INSERT_PADDING_WORDS(0x13);
            };
            std::array<u32, NUM_REGS> reg_array;
        };
    } regs{};

private:
    Core::System& system;

    VideoCore::RasterizerInterface& rasterizer;

    MemoryManager& memory_manager;

    std::vector<u8> read_buffer;
    std::vector<u8> write_buffer;

    /// Performs the copy from the source buffer to the destination buffer as configured in the
    /// registers.
    void HandleCopy();
};

#define ASSERT_REG_POSITION(field_name, position)                                                  \
    static_assert(offsetof(MaxwellDMA::Regs, field_name) == position * 4,                          \
                  "Field " #field_name " has invalid position")

ASSERT_REG_POSITION(exec, 0xC0);
ASSERT_REG_POSITION(src_address, 0x100);
ASSERT_REG_POSITION(dst_address, 0x102);
ASSERT_REG_POSITION(src_pitch, 0x104);
ASSERT_REG_POSITION(dst_pitch, 0x105);
ASSERT_REG_POSITION(x_count, 0x106);
ASSERT_REG_POSITION(y_count, 0x107);
ASSERT_REG_POSITION(const0, 0x1C0);
ASSERT_REG_POSITION(const1, 0x1C1);
ASSERT_REG_POSITION(swizzle_config, 0x1C2);
ASSERT_REG_POSITION(dst_params, 0x1C3);
ASSERT_REG_POSITION(src_params, 0x1CA);

#undef ASSERT_REG_POSITION

} // namespace Tegra::Engines
