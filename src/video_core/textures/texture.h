// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/bit_field.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "video_core/memory_manager.h"

namespace Tegra {
namespace Texture {

enum class TextureFormat : u32 {
    A8R8G8B8 = 0x8,
    A1B5G5R5 = 0x14,
    B5G6R5 = 0x15,
    BF10GF11RF11 = 0x21,
    // Compressed Textures
    BC1 = 0x24,
    BC2 = 0x25,
    BC3 = 0x26,
};

enum class TextureType : u32 {
    Texture1D = 0,
    Texture2D = 1,
    Texture3D = 2,
    TextureCubemap = 3,
    Texture1DArray = 4,
    Texture2DArray = 5,
    Texture1DBuffer = 6,
    Texture2DNoMipmap = 7,
    TextureCubeArray = 8,
};

enum class TICHeaderVersion : u32 {
    OneDBuffer = 0,
    PitchColorKey = 1,
    Pitch = 2,
    BlockLinear = 3,
    BlockLinearColorKey = 4,
};

enum class ComponentType : u32 {
    SNORM = 1,
    UNORM = 2,
    SINT = 3,
    UINT = 4,
    SNORM_FORCE_FP16 = 5,
    UNORM_FORCE_FP16 = 6,
    FLOAT = 7
};

union TextureHandle {
    u32 raw;
    BitField<0, 20, u32> tic_id;
    BitField<20, 12, u32> tsc_id;
};
static_assert(sizeof(TextureHandle) == 4, "TextureHandle has wrong size");

struct TICEntry {
    union {
        u32 raw;
        BitField<0, 7, TextureFormat> format;
        BitField<7, 3, ComponentType> r_type;
        BitField<10, 3, ComponentType> g_type;
        BitField<13, 3, ComponentType> b_type;
        BitField<16, 3, ComponentType> a_type;
    };
    u32 address_low;
    union {
        BitField<0, 16, u32> address_high;
        BitField<21, 3, TICHeaderVersion> header_version;
    };
    union {
        BitField<3, 3, u8> gobs_per_block;
    };
    INSERT_PADDING_BYTES(3);
    union {
        BitField<0, 16, u32> width_minus_1;
        BitField<23, 4, TextureType> texture_type;
    };
    u16 height_minus_1;
    INSERT_PADDING_BYTES(10);

    GPUVAddr Address() const {
        return static_cast<GPUVAddr>((static_cast<GPUVAddr>(address_high) << 32) | address_low);
    }

    u32 Width() const {
        return width_minus_1 + 1;
    }

    u32 Height() const {
        return height_minus_1 + 1;
    }

    bool IsTiled() const {
        return header_version == TICHeaderVersion::BlockLinear ||
               header_version == TICHeaderVersion::BlockLinearColorKey;
    }

    u32 BlockHeight() const {
        return 1 << gobs_per_block;
    }
};
static_assert(sizeof(TICEntry) == 0x20, "TICEntry has wrong size");

enum class WrapMode : u32 {
    Wrap = 0,
    Mirror = 1,
    ClampToEdge = 2,
    Border = 3,
    ClampOGL = 4,
    MirrorOnceClampToEdge = 5,
    MirrorOnceBorder = 6,
    MirrorOnceClampOGL = 7,
};

enum class TextureFilter : u32 {
    Nearest = 1,
    Linear = 2,
};

enum class TextureMipmapFilter : u32 {
    None = 1,
    Nearest = 2,
    Linear = 3,
};

struct TSCEntry {
    union {
        BitField<0, 3, WrapMode> wrap_u;
        BitField<3, 3, WrapMode> wrap_v;
        BitField<6, 3, WrapMode> wrap_p;
        BitField<9, 1, u32> depth_compare_enabled;
        BitField<10, 3, u32> depth_compare_func;
    };
    union {
        BitField<0, 2, TextureFilter> mag_filter;
        BitField<4, 2, TextureFilter> min_filter;
        BitField<6, 2, TextureMipmapFilter> mip_filter;
    };
    INSERT_PADDING_BYTES(8);
    u32 border_color_r;
    u32 border_color_g;
    u32 border_color_b;
    u32 border_color_a;
};
static_assert(sizeof(TSCEntry) == 0x20, "TSCEntry has wrong size");

struct FullTextureInfo {
    u32 index;
    TICEntry tic;
    TSCEntry tsc;
    bool enabled;
};

/// Returns the number of bytes per pixel of the input texture format.
u32 BytesPerPixel(TextureFormat format);

} // namespace Texture
} // namespace Tegra
