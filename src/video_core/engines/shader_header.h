// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/bit_field.h"
#include "common/common_funcs.h"
#include "common/common_types.h"

namespace Tegra::Shader {

enum class OutputTopology : u32 {
    PointList = 1,
    LineStrip = 6,
    TriangleStrip = 7,
};

// Documentation in:
// http://download.nvidia.com/open-gpu-doc/Shader-Program-Header/1/Shader-Program-Header.html#ImapTexture
struct Header {
    union {
        BitField<0, 5, u32> sph_type;
        BitField<5, 5, u32> version;
        BitField<10, 4, u32> shader_type;
        BitField<14, 1, u32> mrt_enable;
        BitField<15, 1, u32> kills_pixels;
        BitField<16, 1, u32> does_global_store;
        BitField<17, 4, u32> sass_version;
        BitField<21, 5, u32> reserved;
        BitField<26, 1, u32> does_load_or_store;
        BitField<27, 1, u32> does_fp64;
        BitField<28, 4, u32> stream_out_mask;
    } common0;

    union {
        BitField<0, 24, u32> shader_local_memory_low_size;
        BitField<24, 8, u32> per_patch_attribute_count;
    } common1;

    union {
        BitField<0, 24, u32> shader_local_memory_high_size;
        BitField<24, 8, u32> threads_per_input_primitive;
    } common2;

    union {
        BitField<0, 24, u32> shader_local_memory_crs_size;
        BitField<24, 4, OutputTopology> output_topology;
        BitField<28, 4, u32> reserved;
    } common3;

    union {
        BitField<0, 12, u32> max_output_vertices;
        BitField<12, 8, u32> store_req_start; // NOTE: not used by geometry shaders.
        BitField<24, 4, u32> reserved;
        BitField<12, 8, u32> store_req_end; // NOTE: not used by geometry shaders.
    } common4;

    union {
        struct {
            INSERT_PADDING_BYTES(3);  // ImapSystemValuesA
            INSERT_PADDING_BYTES(1);  // ImapSystemValuesB
            INSERT_PADDING_BYTES(16); // ImapGenericVector[32]
            INSERT_PADDING_BYTES(2);  // ImapColor
            INSERT_PADDING_BYTES(2);  // ImapSystemValuesC
            INSERT_PADDING_BYTES(5);  // ImapFixedFncTexture[10]
            INSERT_PADDING_BYTES(1);  // ImapReserved
            INSERT_PADDING_BYTES(3);  // OmapSystemValuesA
            INSERT_PADDING_BYTES(1);  // OmapSystemValuesB
            INSERT_PADDING_BYTES(16); // OmapGenericVector[32]
            INSERT_PADDING_BYTES(2);  // OmapColor
            INSERT_PADDING_BYTES(2);  // OmapSystemValuesC
            INSERT_PADDING_BYTES(5);  // OmapFixedFncTexture[10]
            INSERT_PADDING_BYTES(1);  // OmapReserved
        } vtg;

        struct {
            INSERT_PADDING_BYTES(3);  // ImapSystemValuesA
            INSERT_PADDING_BYTES(1);  // ImapSystemValuesB
            INSERT_PADDING_BYTES(32); // ImapGenericVector[32]
            INSERT_PADDING_BYTES(2);  // ImapColor
            INSERT_PADDING_BYTES(2);  // ImapSystemValuesC
            INSERT_PADDING_BYTES(10); // ImapFixedFncTexture[10]
            INSERT_PADDING_BYTES(2);  // ImapReserved
            struct {
                u32 target;
                union {
                    BitField<0, 1, u32> sample_mask;
                    BitField<1, 1, u32> depth;
                    BitField<2, 30, u32> reserved;
                };
            } omap;
            bool IsColorComponentOutputEnabled(u32 render_target, u32 component) const {
                const u32 bit = render_target * 4 + component;
                return omap.target & (1 << bit);
            }
        } ps;
    };

    u64 GetLocalMemorySize() {
        return (common1.shader_local_memory_low_size |
                (common2.shader_local_memory_high_size << 24));
    }
};

static_assert(sizeof(Header) == 0x50, "Incorrect structure size");

} // namespace Tegra::Shader
