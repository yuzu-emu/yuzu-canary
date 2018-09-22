// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include "common/assert.h"
#include "common/bit_field.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "video_core/memory_manager.h"

namespace VideoCore {
class RasterizerInterface;
}

namespace Tegra::Engines {

#define KEPLERMEMORY_REG_INDEX(field_name)                                                         \
    (offsetof(Tegra::Engines::KeplerMemory::Regs, field_name) / sizeof(u32))

class KeplerMemory final {
public:
    KeplerMemory(VideoCore::RasterizerInterface& rasterizer, MemoryManager& memory_manager);
    ~KeplerMemory();

    /// Write the value to the register identified by method.
    void WriteReg(u32 method, u32 value);

    struct Regs {
        static constexpr size_t NUM_REGS = 0x7F;

        union {
            struct {
                INSERT_PADDING_WORDS(0x60);

                u32 line_length_in;
                u32 line_count;

                struct {
                    u32 address_high;
                    u32 address_low;
                    u32 pitch;
                    u32 block_dimensions;
                    u32 width;
                    u32 height;
                    u32 depth;
                    u32 z;
                    u32 x;
                    u32 y;

                    GPUVAddr Address() const {
                        return static_cast<GPUVAddr>((static_cast<GPUVAddr>(address_high) << 32) |
                                                     address_low);
                    }
                } dest;

                struct {
                    union {
                        BitField<0, 1, u32> linear;
                    };
                } exec;

                u32 data;

                INSERT_PADDING_WORDS(0x11);
            };
            std::array<u32, NUM_REGS> reg_array;
        };
    } regs{};

    struct {
        u32 write_offset = 0;
    } state{};

private:
    MemoryManager& memory_manager;
    VideoCore::RasterizerInterface& rasterizer;

    void ProcessData(u32 data);
};

#define ASSERT_REG_POSITION(field_name, position)                                                  \
    static_assert(offsetof(KeplerMemory::Regs, field_name) == position * 4,                        \
                  "Field " #field_name " has invalid position")

ASSERT_REG_POSITION(line_length_in, 0x60);
ASSERT_REG_POSITION(line_count, 0x61);
ASSERT_REG_POSITION(dest, 0x62);
ASSERT_REG_POSITION(exec, 0x6C);
ASSERT_REG_POSITION(data, 0x6D);
#undef ASSERT_REG_POSITION

} // namespace Tegra::Engines
