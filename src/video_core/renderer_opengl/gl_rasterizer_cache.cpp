// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <glad/glad.h>

#include "common/alignment.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "common/microprofile.h"
#include "common/scope_exit.h"
#include "core/core.h"
#include "core/hle/kernel/process.h"
#include "core/memory.h"
#include "core/settings.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/renderer_opengl/gl_rasterizer_cache.h"
#include "video_core/renderer_opengl/gl_state.h"
#include "video_core/renderer_opengl/utils.h"
#include "video_core/surface.h"
#include "video_core/textures/astc.h"
#include "video_core/textures/decoders.h"
#include "video_core/utils.h"

namespace OpenGL {

using VideoCore::Surface::ComponentTypeFromDepthFormat;
using VideoCore::Surface::ComponentTypeFromRenderTarget;
using VideoCore::Surface::ComponentTypeFromTexture;
using VideoCore::Surface::PixelFormatFromDepthFormat;
using VideoCore::Surface::PixelFormatFromRenderTargetFormat;
using VideoCore::Surface::PixelFormatFromTextureFormat;
using VideoCore::Surface::SurfaceTargetFromTextureType;

struct FormatTuple {
    GLint internal_format;
    GLenum format;
    GLenum type;
    ComponentType component_type;
    bool compressed;
};

void SurfaceParams::InitCacheParameters(Tegra::GPUVAddr gpu_addr_) {
    auto& memory_manager{Core::System::GetInstance().GPU().MemoryManager()};
    const auto cpu_addr{memory_manager.GpuToCpuAddress(gpu_addr_)};

    addr = cpu_addr ? *cpu_addr : 0;
    gpu_addr = gpu_addr_;
    size_in_bytes = SizeInBytesRaw();

    if (IsPixelFormatASTC(pixel_format)) {
        // ASTC is uncompressed in software, in emulated as RGBA8
        size_in_bytes_gl = width * height * depth * 4;
    } else {
        size_in_bytes_gl = SizeInBytesGL();
    }
}

std::size_t SurfaceParams::InnerMipmapMemorySize(u32 mip_level, bool force_gl, bool layer_only,
                                                 bool uncompressed) const {
    const u32 tile_x{GetDefaultBlockWidth(pixel_format)};
    const u32 tile_y{GetDefaultBlockHeight(pixel_format)};
    const u32 bytes_per_pixel{GetBytesPerPixel(pixel_format)};
    u32 m_depth = (layer_only ? 1U : depth);
    u32 m_width = MipWidth(mip_level);
    u32 m_height = MipHeight(mip_level);
    m_width = uncompressed ? m_width : std::max(1U, (m_width + tile_x - 1) / tile_x);
    m_height = uncompressed ? m_height : std::max(1U, (m_height + tile_y - 1) / tile_y);
    m_depth = std::max(1U, m_depth >> mip_level);
    u32 m_block_height = MipBlockHeight(mip_level);
    u32 m_block_depth = MipBlockDepth(mip_level);
    return Tegra::Texture::CalculateSize(force_gl ? false : is_tiled, bytes_per_pixel, m_width,
                                         m_height, m_depth, m_block_height, m_block_depth);
}

std::size_t SurfaceParams::InnerMemorySize(bool force_gl, bool layer_only,
                                           bool uncompressed) const {
    std::size_t block_size_bytes = Tegra::Texture::GetGOBSize() * block_height * block_depth;
    std::size_t size = 0;
    for (u32 i = 0; i < max_mip_level; i++) {
        size += InnerMipmapMemorySize(i, force_gl, layer_only, uncompressed);
    }
    if (!force_gl && is_tiled) {
        size = Common::AlignUp(size, block_size_bytes);
    }
    return size;
}

/*static*/ SurfaceParams SurfaceParams::CreateForTexture(
    const Tegra::Texture::FullTextureInfo& config, const GLShader::SamplerEntry& entry) {
    SurfaceParams params{};
    params.is_tiled = config.tic.IsTiled();
    params.block_width = params.is_tiled ? config.tic.BlockWidth() : 0,
    params.block_height = params.is_tiled ? config.tic.BlockHeight() : 0,
    params.block_depth = params.is_tiled ? config.tic.BlockDepth() : 0,
    params.srgb_conversion = config.tic.IsSrgbConversionEnabled();
    params.pixel_format = PixelFormatFromTextureFormat(config.tic.format, config.tic.r_type.Value(),
                                                       params.srgb_conversion);
    params.component_type = ComponentTypeFromTexture(config.tic.r_type.Value());
    params.type = GetFormatType(params.pixel_format);
    params.width = Common::AlignUp(config.tic.Width(), GetCompressionFactor(params.pixel_format));
    params.height = Common::AlignUp(config.tic.Height(), GetCompressionFactor(params.pixel_format));
    params.unaligned_height = config.tic.Height();
    params.target = SurfaceTargetFromTextureType(config.tic.texture_type);

    switch (params.target) {
    case SurfaceTarget::Texture1D:
    case SurfaceTarget::Texture2D:
        params.depth = 1;
        break;
    case SurfaceTarget::TextureCubemap:
        params.depth = config.tic.Depth() * 6;
        break;
    case SurfaceTarget::Texture3D:
        params.depth = config.tic.Depth();
        break;
    case SurfaceTarget::Texture2DArray:
        params.depth = config.tic.Depth();
        if (!entry.IsArray()) {
            // TODO(bunnei): We have seen games re-use a Texture2D as Texture2DArray with depth of
            // one, but sample the texture in the shader as if it were not an array texture. This
            // probably is valid on hardware, but we still need to write a test to confirm this. In
            // emulation, the workaround here is to continue to treat this as a Texture2D. An
            // example game that does this is Super Mario Odyssey (in Cloud Kingdom).
            ASSERT(params.depth == 1);
            params.target = SurfaceTarget::Texture2D;
        }
        break;
    case SurfaceTarget::TextureCubeArray:
        params.depth = config.tic.Depth() * 6;
        if (!entry.IsArray()) {
            ASSERT(params.depth == 6);
            params.target = SurfaceTarget::TextureCubemap;
        }
        break;
    default:
        LOG_CRITICAL(HW_GPU, "Unknown depth for target={}", static_cast<u32>(params.target));
        UNREACHABLE();
        params.depth = 1;
        break;
    }

    params.is_layered = SurfaceTargetIsLayered(params.target);
    params.max_mip_level = config.tic.max_mip_level + 1;
    params.rt = {};

    params.InitCacheParameters(config.tic.Address());

    return params;
}

/*static*/ SurfaceParams SurfaceParams::CreateForFramebuffer(std::size_t index) {
    const auto& config{Core::System::GetInstance().GPU().Maxwell3D().regs.rt[index]};
    SurfaceParams params{};

    params.is_tiled =
        config.memory_layout.type == Tegra::Engines::Maxwell3D::Regs::InvMemoryLayout::BlockLinear;
    params.block_width = 1 << config.memory_layout.block_width;
    params.block_height = 1 << config.memory_layout.block_height;
    params.block_depth = 1 << config.memory_layout.block_depth;
    params.pixel_format = PixelFormatFromRenderTargetFormat(config.format);
    params.srgb_conversion = config.format == Tegra::RenderTargetFormat::BGRA8_SRGB ||
                             config.format == Tegra::RenderTargetFormat::RGBA8_SRGB;
    params.component_type = ComponentTypeFromRenderTarget(config.format);
    params.type = GetFormatType(params.pixel_format);
    params.width = config.width;
    params.height = config.height;
    params.unaligned_height = config.height;
    params.target = SurfaceTarget::Texture2D;
    params.depth = 1;
    params.max_mip_level = 1;
    params.is_layered = false;

    // Render target specific parameters, not used for caching
    params.rt.index = static_cast<u32>(index);
    params.rt.array_mode = config.array_mode;
    params.rt.layer_stride = config.layer_stride;
    params.rt.volume = config.volume;
    params.rt.base_layer = config.base_layer;

    params.InitCacheParameters(config.Address());

    return params;
}

/*static*/ SurfaceParams SurfaceParams::CreateForDepthBuffer(
    u32 zeta_width, u32 zeta_height, Tegra::GPUVAddr zeta_address, Tegra::DepthFormat format,
    u32 block_width, u32 block_height, u32 block_depth,
    Tegra::Engines::Maxwell3D::Regs::InvMemoryLayout type) {
    SurfaceParams params{};

    params.is_tiled = type == Tegra::Engines::Maxwell3D::Regs::InvMemoryLayout::BlockLinear;
    params.block_width = 1 << std::min(block_width, 5U);
    params.block_height = 1 << std::min(block_height, 5U);
    params.block_depth = 1 << std::min(block_depth, 5U);
    params.pixel_format = PixelFormatFromDepthFormat(format);
    params.component_type = ComponentTypeFromDepthFormat(format);
    params.type = GetFormatType(params.pixel_format);
    params.srgb_conversion = false;
    params.width = zeta_width;
    params.height = zeta_height;
    params.unaligned_height = zeta_height;
    params.target = SurfaceTarget::Texture2D;
    params.depth = 1;
    params.max_mip_level = 1;
    params.is_layered = false;
    params.rt = {};

    params.InitCacheParameters(zeta_address);

    return params;
}

/*static*/ SurfaceParams SurfaceParams::CreateForFermiCopySurface(
    const Tegra::Engines::Fermi2D::Regs::Surface& config) {
    SurfaceParams params{};

    params.is_tiled = !config.linear;
    params.block_width = params.is_tiled ? std::min(config.BlockWidth(), 32U) : 0,
    params.block_height = params.is_tiled ? std::min(config.BlockHeight(), 32U) : 0,
    params.block_depth = params.is_tiled ? std::min(config.BlockDepth(), 32U) : 0,
    params.pixel_format = PixelFormatFromRenderTargetFormat(config.format);
    params.srgb_conversion = config.format == Tegra::RenderTargetFormat::BGRA8_SRGB ||
                             config.format == Tegra::RenderTargetFormat::RGBA8_SRGB;
    params.component_type = ComponentTypeFromRenderTarget(config.format);
    params.type = GetFormatType(params.pixel_format);
    params.width = config.width;
    params.height = config.height;
    params.unaligned_height = config.height;
    params.target = SurfaceTarget::Texture2D;
    params.depth = 1;
    params.max_mip_level = 1;
    params.rt = {};

    params.InitCacheParameters(config.Address());

    return params;
}

static constexpr std::array<FormatTuple, VideoCore::Surface::MaxPixelFormat> tex_format_tuples = {{
    {GL_RGBA8, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, ComponentType::UNorm, false}, // ABGR8U
    {GL_RGBA8, GL_RGBA, GL_BYTE, ComponentType::SNorm, false},                     // ABGR8S
    {GL_RGBA8UI, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, ComponentType::UInt, false},   // ABGR8UI
    {GL_RGB8, GL_RGB, GL_UNSIGNED_SHORT_5_6_5_REV, ComponentType::UNorm, false},   // B5G6R5U
    {GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, ComponentType::UNorm,
     false}, // A2B10G10R10U
    {GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, ComponentType::UNorm, false}, // A1B5G5R5U
    {GL_R8, GL_RED, GL_UNSIGNED_BYTE, ComponentType::UNorm, false},                    // R8U
    {GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, ComponentType::UInt, false},           // R8UI
    {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, ComponentType::Float, false},                 // RGBA16F
    {GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT, ComponentType::UNorm, false},              // RGBA16U
    {GL_RGBA16UI, GL_RGBA, GL_UNSIGNED_SHORT, ComponentType::UInt, false},             // RGBA16UI
    {GL_R11F_G11F_B10F, GL_RGB, GL_UNSIGNED_INT_10F_11F_11F_REV, ComponentType::Float,
     false},                                                                     // R11FG11FB10F
    {GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT, ComponentType::UInt, false}, // RGBA32UI
    {GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, ComponentType::UNorm,
     true}, // DXT1
    {GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, ComponentType::UNorm,
     true}, // DXT23
    {GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, ComponentType::UNorm,
     true},                                                                                 // DXT45
    {GL_COMPRESSED_RED_RGTC1, GL_RED, GL_UNSIGNED_INT_8_8_8_8, ComponentType::UNorm, true}, // DXN1
    {GL_COMPRESSED_RG_RGTC2, GL_RG, GL_UNSIGNED_INT_8_8_8_8, ComponentType::UNorm,
     true},                                                                     // DXN2UNORM
    {GL_COMPRESSED_SIGNED_RG_RGTC2, GL_RG, GL_INT, ComponentType::SNorm, true}, // DXN2SNORM
    {GL_COMPRESSED_RGBA_BPTC_UNORM, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, ComponentType::UNorm,
     true}, // BC7U
    {GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT, GL_RGB, GL_UNSIGNED_INT_8_8_8_8, ComponentType::Float,
     true}, // BC6H_UF16
    {GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT, GL_RGB, GL_UNSIGNED_INT_8_8_8_8, ComponentType::Float,
     true},                                                                    // BC6H_SF16
    {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, ComponentType::UNorm, false},        // ASTC_2D_4X4
    {GL_RG8, GL_RG, GL_UNSIGNED_BYTE, ComponentType::UNorm, false},            // G8R8U
    {GL_RG8, GL_RG, GL_BYTE, ComponentType::SNorm, false},                     // G8R8S
    {GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE, ComponentType::UNorm, false},        // BGRA8
    {GL_RGBA32F, GL_RGBA, GL_FLOAT, ComponentType::Float, false},              // RGBA32F
    {GL_RG32F, GL_RG, GL_FLOAT, ComponentType::Float, false},                  // RG32F
    {GL_R32F, GL_RED, GL_FLOAT, ComponentType::Float, false},                  // R32F
    {GL_R16F, GL_RED, GL_HALF_FLOAT, ComponentType::Float, false},             // R16F
    {GL_R16, GL_RED, GL_UNSIGNED_SHORT, ComponentType::UNorm, false},          // R16U
    {GL_R16_SNORM, GL_RED, GL_SHORT, ComponentType::SNorm, false},             // R16S
    {GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT, ComponentType::UInt, false}, // R16UI
    {GL_R16I, GL_RED_INTEGER, GL_SHORT, ComponentType::SInt, false},           // R16I
    {GL_RG16, GL_RG, GL_UNSIGNED_SHORT, ComponentType::UNorm, false},          // RG16
    {GL_RG16F, GL_RG, GL_HALF_FLOAT, ComponentType::Float, false},             // RG16F
    {GL_RG16UI, GL_RG_INTEGER, GL_UNSIGNED_SHORT, ComponentType::UInt, false}, // RG16UI
    {GL_RG16I, GL_RG_INTEGER, GL_SHORT, ComponentType::SInt, false},           // RG16I
    {GL_RG16_SNORM, GL_RG, GL_SHORT, ComponentType::SNorm, false},             // RG16S
    {GL_RGB32F, GL_RGB, GL_FLOAT, ComponentType::Float, false},                // RGB32F
    {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, ComponentType::UNorm,
     false},                                                                   // RGBA8_SRGB
    {GL_RG8, GL_RG, GL_UNSIGNED_BYTE, ComponentType::UNorm, false},            // RG8U
    {GL_RG8, GL_RG, GL_BYTE, ComponentType::SNorm, false},                     // RG8S
    {GL_RG32UI, GL_RG_INTEGER, GL_UNSIGNED_INT, ComponentType::UInt, false},   // RG32UI
    {GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, ComponentType::UInt, false},   // R32UI
    {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, ComponentType::UNorm, false},        // ASTC_2D_8X8
    {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, ComponentType::UNorm, false},        // ASTC_2D_8X5
    {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, ComponentType::UNorm, false},        // ASTC_2D_5X4
    {GL_SRGB8_ALPHA8, GL_BGRA, GL_UNSIGNED_BYTE, ComponentType::UNorm, false}, // BGRA8
    // Compressed sRGB formats
    {GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, ComponentType::UNorm,
     true}, // DXT1_SRGB
    {GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, ComponentType::UNorm,
     true}, // DXT23_SRGB
    {GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, ComponentType::UNorm,
     true}, // DXT45_SRGB
    {GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, ComponentType::UNorm,
     true},                                                                    // BC7U_SRGB
    {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, ComponentType::UNorm, false}, // ASTC_2D_4X4_SRGB
    {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, ComponentType::UNorm, false}, // ASTC_2D_8X8_SRGB
    {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, ComponentType::UNorm, false}, // ASTC_2D_8X5_SRGB
    {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, ComponentType::UNorm, false}, // ASTC_2D_5X4_SRGB
    {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, ComponentType::UNorm, false},        // ASTC_2D_5X5
    {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, ComponentType::UNorm, false}, // ASTC_2D_5X5_SRGB
    {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, ComponentType::UNorm, false},        // ASTC_2D_10X8
    {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, ComponentType::UNorm, false}, // ASTC_2D_10X8_SRGB

    // Depth formats
    {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, ComponentType::Float, false}, // Z32F
    {GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, ComponentType::UNorm,
     false}, // Z16

    // DepthStencil formats
    {GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, ComponentType::UNorm,
     false}, // Z24S8
    {GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, ComponentType::UNorm,
     false}, // S8Z24
    {GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV,
     ComponentType::Float, false}, // Z32FS8
}};

static GLenum SurfaceTargetToGL(SurfaceTarget target) {
    switch (target) {
    case SurfaceTarget::Texture1D:
        return GL_TEXTURE_1D;
    case SurfaceTarget::Texture2D:
        return GL_TEXTURE_2D;
    case SurfaceTarget::Texture3D:
        return GL_TEXTURE_3D;
    case SurfaceTarget::Texture1DArray:
        return GL_TEXTURE_1D_ARRAY;
    case SurfaceTarget::Texture2DArray:
        return GL_TEXTURE_2D_ARRAY;
    case SurfaceTarget::TextureCubemap:
        return GL_TEXTURE_CUBE_MAP;
    case SurfaceTarget::TextureCubeArray:
        return GL_TEXTURE_CUBE_MAP_ARRAY;
    }
    LOG_CRITICAL(Render_OpenGL, "Unimplemented texture target={}", static_cast<u32>(target));
    UNREACHABLE();
    return {};
}

static const FormatTuple& GetFormatTuple(PixelFormat pixel_format, ComponentType component_type) {
    ASSERT(static_cast<std::size_t>(pixel_format) < tex_format_tuples.size());
    auto& format = tex_format_tuples[static_cast<unsigned int>(pixel_format)];
    ASSERT(component_type == format.component_type);

    return format;
}

MathUtil::Rectangle<u32> SurfaceParams::GetRect(u32 mip_level) const {
    u32 actual_height{std::max(1U, unaligned_height >> mip_level)};
    if (IsPixelFormatASTC(pixel_format)) {
        // ASTC formats must stop at the ATSC block size boundary
        actual_height = Common::AlignDown(actual_height, GetASTCBlockSize(pixel_format).second);
    }
    return {0, actual_height, MipWidth(mip_level), 0};
}

template <bool morton_to_gl, PixelFormat format>
void MortonCopy(u32 stride, u32 block_height, u32 height, u32 block_depth, u32 depth, u8* gl_buffer,
                std::size_t gl_buffer_size, VAddr addr) {
    constexpr u32 bytes_per_pixel = GetBytesPerPixel(format);

    // With the BCn formats (DXT and DXN), each 4x4 tile is swizzled instead of just individual
    // pixel values.
    const u32 tile_size_x{GetDefaultBlockWidth(format)};
    const u32 tile_size_y{GetDefaultBlockHeight(format)};

    if (morton_to_gl) {
        Tegra::Texture::UnswizzleTexture(gl_buffer, addr, tile_size_x, tile_size_y, bytes_per_pixel,
                                         stride, height, depth, block_height, block_depth);
    } else {
        Tegra::Texture::CopySwizzledData((stride + tile_size_x - 1) / tile_size_x,
                                         (height + tile_size_y - 1) / tile_size_y, depth,
                                         bytes_per_pixel, bytes_per_pixel, Memory::GetPointer(addr),
                                         gl_buffer, false, block_height, block_depth);
    }
}

using GLConversionArray = std::array<void (*)(u32, u32, u32, u32, u32, u8*, std::size_t, VAddr),
                                     VideoCore::Surface::MaxPixelFormat>;

static constexpr GLConversionArray morton_to_gl_fns = {
    // clang-format off
        MortonCopy<true, PixelFormat::ABGR8U>,
        MortonCopy<true, PixelFormat::ABGR8S>,
        MortonCopy<true, PixelFormat::ABGR8UI>,
        MortonCopy<true, PixelFormat::B5G6R5U>,
        MortonCopy<true, PixelFormat::A2B10G10R10U>,
        MortonCopy<true, PixelFormat::A1B5G5R5U>,
        MortonCopy<true, PixelFormat::R8U>,
        MortonCopy<true, PixelFormat::R8UI>,
        MortonCopy<true, PixelFormat::RGBA16F>,
        MortonCopy<true, PixelFormat::RGBA16U>,
        MortonCopy<true, PixelFormat::RGBA16UI>,
        MortonCopy<true, PixelFormat::R11FG11FB10F>,
        MortonCopy<true, PixelFormat::RGBA32UI>,
        MortonCopy<true, PixelFormat::DXT1>,
        MortonCopy<true, PixelFormat::DXT23>,
        MortonCopy<true, PixelFormat::DXT45>,
        MortonCopy<true, PixelFormat::DXN1>,
        MortonCopy<true, PixelFormat::DXN2UNORM>,
        MortonCopy<true, PixelFormat::DXN2SNORM>,
        MortonCopy<true, PixelFormat::BC7U>,
        MortonCopy<true, PixelFormat::BC6H_UF16>,
        MortonCopy<true, PixelFormat::BC6H_SF16>,
        MortonCopy<true, PixelFormat::ASTC_2D_4X4>,
        MortonCopy<true, PixelFormat::G8R8U>,
        MortonCopy<true, PixelFormat::G8R8S>,
        MortonCopy<true, PixelFormat::BGRA8>,
        MortonCopy<true, PixelFormat::RGBA32F>,
        MortonCopy<true, PixelFormat::RG32F>,
        MortonCopy<true, PixelFormat::R32F>,
        MortonCopy<true, PixelFormat::R16F>,
        MortonCopy<true, PixelFormat::R16U>,
        MortonCopy<true, PixelFormat::R16S>,
        MortonCopy<true, PixelFormat::R16UI>,
        MortonCopy<true, PixelFormat::R16I>,
        MortonCopy<true, PixelFormat::RG16>,
        MortonCopy<true, PixelFormat::RG16F>,
        MortonCopy<true, PixelFormat::RG16UI>,
        MortonCopy<true, PixelFormat::RG16I>,
        MortonCopy<true, PixelFormat::RG16S>,
        MortonCopy<true, PixelFormat::RGB32F>,
        MortonCopy<true, PixelFormat::RGBA8_SRGB>,
        MortonCopy<true, PixelFormat::RG8U>,
        MortonCopy<true, PixelFormat::RG8S>,
        MortonCopy<true, PixelFormat::RG32UI>,
        MortonCopy<true, PixelFormat::R32UI>,
        MortonCopy<true, PixelFormat::ASTC_2D_8X8>,
        MortonCopy<true, PixelFormat::ASTC_2D_8X5>,
        MortonCopy<true, PixelFormat::ASTC_2D_5X4>,
        MortonCopy<true, PixelFormat::BGRA8_SRGB>,
        MortonCopy<true, PixelFormat::DXT1_SRGB>,
        MortonCopy<true, PixelFormat::DXT23_SRGB>,
        MortonCopy<true, PixelFormat::DXT45_SRGB>,
        MortonCopy<true, PixelFormat::BC7U_SRGB>,
        MortonCopy<true, PixelFormat::ASTC_2D_4X4_SRGB>,
        MortonCopy<true, PixelFormat::ASTC_2D_8X8_SRGB>,
        MortonCopy<true, PixelFormat::ASTC_2D_8X5_SRGB>,
        MortonCopy<true, PixelFormat::ASTC_2D_5X4_SRGB>,
        MortonCopy<true, PixelFormat::ASTC_2D_5X5>,
        MortonCopy<true, PixelFormat::ASTC_2D_5X5_SRGB>,
        MortonCopy<true, PixelFormat::ASTC_2D_10X8>,
        MortonCopy<true, PixelFormat::ASTC_2D_10X8_SRGB>,
        MortonCopy<true, PixelFormat::Z32F>,
        MortonCopy<true, PixelFormat::Z16>,
        MortonCopy<true, PixelFormat::Z24S8>,
        MortonCopy<true, PixelFormat::S8Z24>,
        MortonCopy<true, PixelFormat::Z32FS8>,
    // clang-format on
};

static constexpr GLConversionArray gl_to_morton_fns = {
    // clang-format off
        MortonCopy<false, PixelFormat::ABGR8U>,
        MortonCopy<false, PixelFormat::ABGR8S>,
        MortonCopy<false, PixelFormat::ABGR8UI>,
        MortonCopy<false, PixelFormat::B5G6R5U>,
        MortonCopy<false, PixelFormat::A2B10G10R10U>,
        MortonCopy<false, PixelFormat::A1B5G5R5U>,
        MortonCopy<false, PixelFormat::R8U>,
        MortonCopy<false, PixelFormat::R8UI>,
        MortonCopy<false, PixelFormat::RGBA16F>,
        MortonCopy<false, PixelFormat::RGBA16U>,
        MortonCopy<false, PixelFormat::RGBA16UI>,
        MortonCopy<false, PixelFormat::R11FG11FB10F>,
        MortonCopy<false, PixelFormat::RGBA32UI>,
        MortonCopy<false, PixelFormat::DXT1>,
        MortonCopy<false, PixelFormat::DXT23>,
        MortonCopy<false, PixelFormat::DXT45>,
        MortonCopy<false, PixelFormat::DXN1>,
        MortonCopy<false, PixelFormat::DXN2UNORM>,
        MortonCopy<false, PixelFormat::DXN2SNORM>,
        MortonCopy<false, PixelFormat::BC7U>,
        MortonCopy<false, PixelFormat::BC6H_UF16>,
        MortonCopy<false, PixelFormat::BC6H_SF16>,
        // TODO(Subv): Swizzling ASTC formats are not supported
        nullptr,
        MortonCopy<false, PixelFormat::G8R8U>,
        MortonCopy<false, PixelFormat::G8R8S>,
        MortonCopy<false, PixelFormat::BGRA8>,
        MortonCopy<false, PixelFormat::RGBA32F>,
        MortonCopy<false, PixelFormat::RG32F>,
        MortonCopy<false, PixelFormat::R32F>,
        MortonCopy<false, PixelFormat::R16F>,
        MortonCopy<false, PixelFormat::R16U>,
        MortonCopy<false, PixelFormat::R16S>,
        MortonCopy<false, PixelFormat::R16UI>,
        MortonCopy<false, PixelFormat::R16I>,
        MortonCopy<false, PixelFormat::RG16>,
        MortonCopy<false, PixelFormat::RG16F>,
        MortonCopy<false, PixelFormat::RG16UI>,
        MortonCopy<false, PixelFormat::RG16I>,
        MortonCopy<false, PixelFormat::RG16S>,
        MortonCopy<false, PixelFormat::RGB32F>,
        MortonCopy<false, PixelFormat::RGBA8_SRGB>,
        MortonCopy<false, PixelFormat::RG8U>,
        MortonCopy<false, PixelFormat::RG8S>,
        MortonCopy<false, PixelFormat::RG32UI>,
        MortonCopy<false, PixelFormat::R32UI>,
        nullptr,
        nullptr,
        nullptr,
        MortonCopy<false, PixelFormat::BGRA8_SRGB>,
        MortonCopy<false, PixelFormat::DXT1_SRGB>,
        MortonCopy<false, PixelFormat::DXT23_SRGB>,
        MortonCopy<false, PixelFormat::DXT45_SRGB>,
        MortonCopy<false, PixelFormat::BC7U_SRGB>,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        MortonCopy<false, PixelFormat::Z32F>,
        MortonCopy<false, PixelFormat::Z16>,
        MortonCopy<false, PixelFormat::Z24S8>,
        MortonCopy<false, PixelFormat::S8Z24>,
        MortonCopy<false, PixelFormat::Z32FS8>,
    // clang-format on
};

void SwizzleFunc(const GLConversionArray& functions, const SurfaceParams& params,
                 std::vector<u8>& gl_buffer, u32 mip_level) {
    u32 depth = params.MipDepth(mip_level);
    if (params.target == SurfaceTarget::Texture2D) {
        // TODO(Blinkhawk): Eliminate this condition once all texture types are implemented.
        depth = 1U;
    }
    if (params.is_layered) {
        u64 offset = params.GetMipmapLevelOffset(mip_level);
        u64 offset_gl = 0;
        const u64 layer_size = params.LayerMemorySize();
        const u64 gl_size = params.LayerSizeGL(mip_level);
        for (u32 i = 0; i < params.depth; i++) {
            functions[static_cast<std::size_t>(params.pixel_format)](
                params.MipWidth(mip_level), params.MipBlockHeight(mip_level),
                params.MipHeight(mip_level), params.MipBlockDepth(mip_level), 1,
                gl_buffer.data() + offset_gl, gl_size, params.addr + offset);
            offset += layer_size;
            offset_gl += gl_size;
        }
    } else {
        const u64 offset = params.GetMipmapLevelOffset(mip_level);
        functions[static_cast<std::size_t>(params.pixel_format)](
            params.MipWidth(mip_level), params.MipBlockHeight(mip_level),
            params.MipHeight(mip_level), params.MipBlockDepth(mip_level), depth, gl_buffer.data(),
            gl_buffer.size(), params.addr + offset);
    }
}

MICROPROFILE_DEFINE(OpenGL_BlitSurface, "OpenGL", "BlitSurface", MP_RGB(128, 192, 64));
static bool BlitSurface(const Surface& src_surface, const Surface& dst_surface,
                        GLuint read_fb_handle, GLuint draw_fb_handle, GLenum src_attachment = 0,
                        GLenum dst_attachment = 0, std::size_t cubemap_face = 0) {
    MICROPROFILE_SCOPE(OpenGL_BlitSurface);

    const auto& src_params{src_surface->GetSurfaceParams()};
    const auto& dst_params{dst_surface->GetSurfaceParams()};

    OpenGLState prev_state{OpenGLState::GetCurState()};
    SCOPE_EXIT({ prev_state.Apply(); });

    OpenGLState state;
    state.draw.read_framebuffer = read_fb_handle;
    state.draw.draw_framebuffer = draw_fb_handle;
    // Set sRGB enabled if the destination surfaces need it
    state.framebuffer_srgb.enabled = dst_params.srgb_conversion;
    state.ApplyFramebufferState();

    u32 buffers{};

    if (src_params.type == SurfaceType::ColorTexture) {
        switch (src_params.target) {
        case SurfaceTarget::Texture2D:
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + src_attachment,
                                   GL_TEXTURE_2D, src_surface->Texture().handle, 0);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                                   0, 0);
            break;
        case SurfaceTarget::TextureCubemap:
            glFramebufferTexture2D(
                GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + src_attachment,
                static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + cubemap_face),
                src_surface->Texture().handle, 0);
            glFramebufferTexture2D(
                GL_READ_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + cubemap_face), 0, 0);
            break;
        case SurfaceTarget::Texture2DArray:
            glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + src_attachment,
                                      src_surface->Texture().handle, 0, 0);
            glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, 0, 0, 0);
            break;
        case SurfaceTarget::Texture3D:
            glFramebufferTexture3D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + src_attachment,
                                   SurfaceTargetToGL(src_params.target),
                                   src_surface->Texture().handle, 0, 0);
            glFramebufferTexture3D(GL_READ_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                   SurfaceTargetToGL(src_params.target), 0, 0, 0);
            break;
        default:
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + src_attachment,
                                   GL_TEXTURE_2D, src_surface->Texture().handle, 0);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                                   0, 0);
            break;
        }

        switch (dst_params.target) {
        case SurfaceTarget::Texture2D:
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + dst_attachment,
                                   GL_TEXTURE_2D, dst_surface->Texture().handle, 0);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                                   0, 0);
            break;
        case SurfaceTarget::TextureCubemap:
            glFramebufferTexture2D(
                GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + dst_attachment,
                static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + cubemap_face),
                dst_surface->Texture().handle, 0);
            glFramebufferTexture2D(
                GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + cubemap_face), 0, 0);
            break;
        case SurfaceTarget::Texture2DArray:
            glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + dst_attachment,
                                      dst_surface->Texture().handle, 0, 0);
            glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, 0, 0, 0);
            break;

        case SurfaceTarget::Texture3D:
            glFramebufferTexture3D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + dst_attachment,
                                   SurfaceTargetToGL(dst_params.target),
                                   dst_surface->Texture().handle, 0, 0);
            glFramebufferTexture3D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                   SurfaceTargetToGL(dst_params.target), 0, 0, 0);
            break;
        default:
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + dst_attachment,
                                   GL_TEXTURE_2D, dst_surface->Texture().handle, 0);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                                   0, 0);
            break;
        }

        buffers = GL_COLOR_BUFFER_BIT;
    } else if (src_params.type == SurfaceType::Depth) {
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + src_attachment,
                               GL_TEXTURE_2D, 0, 0);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                               src_surface->Texture().handle, 0);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);

        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + dst_attachment,
                               GL_TEXTURE_2D, 0, 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                               dst_surface->Texture().handle, 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);

        buffers = GL_DEPTH_BUFFER_BIT;
    } else if (src_params.type == SurfaceType::DepthStencil) {
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + src_attachment,
                               GL_TEXTURE_2D, 0, 0);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                               src_surface->Texture().handle, 0);

        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + dst_attachment,
                               GL_TEXTURE_2D, 0, 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                               dst_surface->Texture().handle, 0);

        buffers = GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
    }

    const auto& rect{src_params.GetRect()};
    glBlitFramebuffer(rect.left, rect.bottom, rect.right, rect.top, rect.left, rect.bottom,
                      rect.right, rect.top, buffers,
                      buffers == GL_COLOR_BUFFER_BIT ? GL_LINEAR : GL_NEAREST);

    return true;
}

static void FastCopySurface(const Surface& src_surface, const Surface& dst_surface) {
    const auto& src_params{src_surface->GetSurfaceParams()};
    const auto& dst_params{dst_surface->GetSurfaceParams()};

    const u32 width{std::min(src_params.width, dst_params.width)};
    const u32 height{std::min(src_params.height, dst_params.height)};

    glCopyImageSubData(src_surface->Texture().handle, SurfaceTargetToGL(src_params.target), 0, 0, 0,
                       0, dst_surface->Texture().handle, SurfaceTargetToGL(dst_params.target), 0, 0,
                       0, 0, width, height, 1);
}

MICROPROFILE_DEFINE(OpenGL_CopySurface, "OpenGL", "CopySurface", MP_RGB(128, 192, 64));
static void CopySurface(const Surface& src_surface, const Surface& dst_surface,
                        const GLuint copy_pbo_handle, const GLenum src_attachment = 0,
                        const GLenum dst_attachment = 0, const std::size_t cubemap_face = 0) {
    MICROPROFILE_SCOPE(OpenGL_CopySurface);
    ASSERT_MSG(dst_attachment == 0, "Unimplemented");

    const auto& src_params{src_surface->GetSurfaceParams()};
    const auto& dst_params{dst_surface->GetSurfaceParams()};

    const auto source_format = GetFormatTuple(src_params.pixel_format, src_params.component_type);
    const auto dest_format = GetFormatTuple(dst_params.pixel_format, dst_params.component_type);

    const std::size_t buffer_size = std::max(src_params.size_in_bytes, dst_params.size_in_bytes);

    glBindBuffer(GL_PIXEL_PACK_BUFFER, copy_pbo_handle);
    glBufferData(GL_PIXEL_PACK_BUFFER, buffer_size, nullptr, GL_STREAM_DRAW);
    if (source_format.compressed) {
        glGetCompressedTextureImage(src_surface->Texture().handle, src_attachment,
                                    static_cast<GLsizei>(src_params.size_in_bytes), nullptr);
    } else {
        glGetTextureImage(src_surface->Texture().handle, src_attachment, source_format.format,
                          source_format.type, static_cast<GLsizei>(src_params.size_in_bytes),
                          nullptr);
    }
    // If the new texture is bigger than the previous one, we need to fill in the rest with data
    // from the CPU.
    if (src_params.size_in_bytes < dst_params.size_in_bytes) {
        // Upload the rest of the memory.
        if (dst_params.is_tiled) {
            // TODO(Subv): We might have to de-tile the subtexture and re-tile it with the rest
            // of the data in this case. Games like Super Mario Odyssey seem to hit this case
            // when drawing, it re-uses the memory of a previous texture as a bigger framebuffer
            // but it doesn't clear it beforehand, the texture is already full of zeros.
            LOG_DEBUG(HW_GPU, "Trying to upload extra texture data from the CPU during "
                              "reinterpretation but the texture is tiled.");
        }
        const std::size_t remaining_size = dst_params.size_in_bytes - src_params.size_in_bytes;

        glBufferSubData(GL_PIXEL_PACK_BUFFER, src_params.size_in_bytes, remaining_size,
                        Memory::GetPointer(dst_params.addr + src_params.size_in_bytes));
    }

    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    const GLsizei width{static_cast<GLsizei>(
        std::min(src_params.GetRect().GetWidth(), dst_params.GetRect().GetWidth()))};
    const GLsizei height{static_cast<GLsizei>(
        std::min(src_params.GetRect().GetHeight(), dst_params.GetRect().GetHeight()))};

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, copy_pbo_handle);
    if (dest_format.compressed) {
        LOG_CRITICAL(HW_GPU, "Compressed copy is unimplemented!");
        UNREACHABLE();
    } else {
        switch (dst_params.target) {
        case SurfaceTarget::Texture1D:
            glTextureSubImage1D(dst_surface->Texture().handle, 0, 0, width, dest_format.format,
                                dest_format.type, nullptr);
            break;
        case SurfaceTarget::Texture2D:
            glTextureSubImage2D(dst_surface->Texture().handle, 0, 0, 0, width, height,
                                dest_format.format, dest_format.type, nullptr);
            break;
        case SurfaceTarget::Texture3D:
        case SurfaceTarget::Texture2DArray:
        case SurfaceTarget::TextureCubeArray:
            glTextureSubImage3D(dst_surface->Texture().handle, 0, 0, 0, 0, width, height,
                                static_cast<GLsizei>(dst_params.depth), dest_format.format,
                                dest_format.type, nullptr);
            break;
        case SurfaceTarget::TextureCubemap:
            glTextureSubImage3D(dst_surface->Texture().handle, 0, 0, 0,
                                static_cast<GLint>(cubemap_face), width, height, 1,
                                dest_format.format, dest_format.type, nullptr);
            break;
        default:
            LOG_CRITICAL(Render_OpenGL, "Unimplemented surface target={}",
                         static_cast<u32>(dst_params.target));
            UNREACHABLE();
        }
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }
}

CachedSurface::CachedSurface(const SurfaceParams& params)
    : params(params), gl_target(SurfaceTargetToGL(params.target)),
      cached_size_in_bytes(params.size_in_bytes) {
    texture.Create();
    const auto& rect{params.GetRect()};

    // Keep track of previous texture bindings
    OpenGLState cur_state = OpenGLState::GetCurState();
    const auto& old_tex = cur_state.texture_units[0];
    SCOPE_EXIT({
        cur_state.texture_units[0] = old_tex;
        cur_state.Apply();
    });

    cur_state.texture_units[0].texture = texture.handle;
    cur_state.texture_units[0].target = SurfaceTargetToGL(params.target);
    cur_state.Apply();
    glActiveTexture(GL_TEXTURE0);

    const auto& format_tuple = GetFormatTuple(params.pixel_format, params.component_type);
    if (!format_tuple.compressed) {
        // Only pre-create the texture for non-compressed textures.
        switch (params.target) {
        case SurfaceTarget::Texture1D:
            glTexStorage1D(SurfaceTargetToGL(params.target), params.max_mip_level,
                           format_tuple.internal_format, rect.GetWidth());
            break;
        case SurfaceTarget::Texture2D:
        case SurfaceTarget::TextureCubemap:
            glTexStorage2D(SurfaceTargetToGL(params.target), params.max_mip_level,
                           format_tuple.internal_format, rect.GetWidth(), rect.GetHeight());
            break;
        case SurfaceTarget::Texture3D:
        case SurfaceTarget::Texture2DArray:
        case SurfaceTarget::TextureCubeArray:
            glTexStorage3D(SurfaceTargetToGL(params.target), params.max_mip_level,
                           format_tuple.internal_format, rect.GetWidth(), rect.GetHeight(),
                           params.depth);
            break;
        default:
            LOG_CRITICAL(Render_OpenGL, "Unimplemented surface target={}",
                         static_cast<u32>(params.target));
            UNREACHABLE();
            glTexStorage2D(GL_TEXTURE_2D, params.max_mip_level, format_tuple.internal_format,
                           rect.GetWidth(), rect.GetHeight());
        }
    }

    glTexParameteri(SurfaceTargetToGL(params.target), GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(SurfaceTargetToGL(params.target), GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(SurfaceTargetToGL(params.target), GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(SurfaceTargetToGL(params.target), GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(SurfaceTargetToGL(params.target), GL_TEXTURE_MAX_LEVEL,
                    params.max_mip_level - 1);
    if (params.max_mip_level == 1) {
        glTexParameterf(SurfaceTargetToGL(params.target), GL_TEXTURE_LOD_BIAS, 1000.0);
    }

    LabelGLObject(GL_TEXTURE, texture.handle, params.addr,
                  SurfaceParams::SurfaceTargetName(params.target));

    // Clamp size to mapped GPU memory region
    // TODO(bunnei): Super Mario Odyssey maps a 0x40000 byte region and then uses it for a 0x80000
    // R32F render buffer. We do not yet know if this is a game bug or something else, but this
    // check is necessary to prevent flushing from overwriting unmapped memory.

    auto& memory_manager{Core::System::GetInstance().GPU().MemoryManager()};
    const u64 max_size{memory_manager.GetRegionEnd(params.gpu_addr) - params.gpu_addr};
    if (cached_size_in_bytes > max_size) {
        LOG_ERROR(HW_GPU, "Surface size {} exceeds region size {}", params.size_in_bytes, max_size);
        cached_size_in_bytes = max_size;
    }
}

static void ConvertS8Z24ToZ24S8(std::vector<u8>& data, u32 width, u32 height, bool reverse) {
    union S8Z24 {
        BitField<0, 24, u32> z24;
        BitField<24, 8, u32> s8;
    };
    static_assert(sizeof(S8Z24) == 4, "S8Z24 is incorrect size");

    union Z24S8 {
        BitField<0, 8, u32> s8;
        BitField<8, 24, u32> z24;
    };
    static_assert(sizeof(Z24S8) == 4, "Z24S8 is incorrect size");

    S8Z24 s8z24_pixel{};
    Z24S8 z24s8_pixel{};
    constexpr auto bpp{GetBytesPerPixel(PixelFormat::S8Z24)};
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const std::size_t offset{bpp * (y * width + x)};
            if (reverse) {
                std::memcpy(&z24s8_pixel, &data[offset], sizeof(Z24S8));
                s8z24_pixel.s8.Assign(z24s8_pixel.s8);
                s8z24_pixel.z24.Assign(z24s8_pixel.z24);
                std::memcpy(&data[offset], &s8z24_pixel, sizeof(S8Z24));
            } else {
                std::memcpy(&s8z24_pixel, &data[offset], sizeof(S8Z24));
                z24s8_pixel.s8.Assign(s8z24_pixel.s8);
                z24s8_pixel.z24.Assign(s8z24_pixel.z24);
                std::memcpy(&data[offset], &z24s8_pixel, sizeof(Z24S8));
            }
        }
    }
}

static void ConvertG8R8ToR8G8(std::vector<u8>& data, u32 width, u32 height) {
    constexpr auto bpp{GetBytesPerPixel(PixelFormat::G8R8U)};
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const std::size_t offset{bpp * (y * width + x)};
            const u8 temp{data[offset]};
            data[offset] = data[offset + 1];
            data[offset + 1] = temp;
        }
    }
}

/**
 * Helper function to perform software conversion (as needed) when loading a buffer from Switch
 * memory. This is for Maxwell pixel formats that cannot be represented as-is in OpenGL or with
 * typical desktop GPUs.
 */
static void ConvertFormatAsNeeded_LoadGLBuffer(std::vector<u8>& data, PixelFormat pixel_format,
                                               u32 width, u32 height, u32 depth) {
    switch (pixel_format) {
    case PixelFormat::ASTC_2D_4X4:
    case PixelFormat::ASTC_2D_8X8:
    case PixelFormat::ASTC_2D_8X5:
    case PixelFormat::ASTC_2D_5X4:
    case PixelFormat::ASTC_2D_5X5:
    case PixelFormat::ASTC_2D_4X4_SRGB:
    case PixelFormat::ASTC_2D_8X8_SRGB:
    case PixelFormat::ASTC_2D_8X5_SRGB:
    case PixelFormat::ASTC_2D_5X4_SRGB:
    case PixelFormat::ASTC_2D_5X5_SRGB:
    case PixelFormat::ASTC_2D_10X8:
    case PixelFormat::ASTC_2D_10X8_SRGB: {
        // Convert ASTC pixel formats to RGBA8, as most desktop GPUs do not support ASTC.
        u32 block_width{};
        u32 block_height{};
        std::tie(block_width, block_height) = GetASTCBlockSize(pixel_format);
        data =
            Tegra::Texture::ASTC::Decompress(data, width, height, depth, block_width, block_height);
        break;
    }
    case PixelFormat::S8Z24:
        // Convert the S8Z24 depth format to Z24S8, as OpenGL does not support S8Z24.
        ConvertS8Z24ToZ24S8(data, width, height, false);
        break;

    case PixelFormat::G8R8U:
    case PixelFormat::G8R8S:
        // Convert the G8R8 color format to R8G8, as OpenGL does not support G8R8.
        ConvertG8R8ToR8G8(data, width, height);
        break;
    }
}

/**
 * Helper function to perform software conversion (as needed) when flushing a buffer from OpenGL to
 * Switch memory. This is for Maxwell pixel formats that cannot be represented as-is in OpenGL or
 * with typical desktop GPUs.
 */
static void ConvertFormatAsNeeded_FlushGLBuffer(std::vector<u8>& data, PixelFormat pixel_format,
                                                u32 width, u32 height) {
    switch (pixel_format) {
    case PixelFormat::G8R8U:
    case PixelFormat::G8R8S:
    case PixelFormat::ASTC_2D_4X4:
    case PixelFormat::ASTC_2D_8X8:
    case PixelFormat::ASTC_2D_4X4_SRGB:
    case PixelFormat::ASTC_2D_8X8_SRGB:
    case PixelFormat::ASTC_2D_5X5:
    case PixelFormat::ASTC_2D_5X5_SRGB:
    case PixelFormat::ASTC_2D_10X8:
    case PixelFormat::ASTC_2D_10X8_SRGB: {
        LOG_CRITICAL(HW_GPU, "Conversion of format {} after texture flushing is not implemented",
                     static_cast<u32>(pixel_format));
        UNREACHABLE();
        break;
    }
    case PixelFormat::S8Z24:
        // Convert the Z24S8 depth format to S8Z24, as OpenGL does not support S8Z24.
        ConvertS8Z24ToZ24S8(data, width, height, true);
        break;
    }
}

MICROPROFILE_DEFINE(OpenGL_SurfaceLoad, "OpenGL", "Surface Load", MP_RGB(128, 192, 64));
void CachedSurface::LoadGLBuffer() {
    MICROPROFILE_SCOPE(OpenGL_SurfaceLoad);
    gl_buffer.resize(params.max_mip_level);
    for (u32 i = 0; i < params.max_mip_level; i++)
        gl_buffer[i].resize(params.GetMipmapSizeGL(i));
    if (params.is_tiled) {
        ASSERT_MSG(params.block_width == 1, "Block width is defined as {} on texture type {}",
                   params.block_width, static_cast<u32>(params.target));
        for (u32 i = 0; i < params.max_mip_level; i++)
            SwizzleFunc(morton_to_gl_fns, params, gl_buffer[i], i);
    } else {
        const auto texture_src_data{Memory::GetPointer(params.addr)};
        const auto texture_src_data_end{texture_src_data + params.size_in_bytes_gl};
        gl_buffer[0].assign(texture_src_data, texture_src_data_end);
    }
    for (u32 i = 0; i < params.max_mip_level; i++)
        ConvertFormatAsNeeded_LoadGLBuffer(gl_buffer[i], params.pixel_format, params.MipWidth(i),
                                           params.MipHeight(i), params.MipDepth(i));
}

MICROPROFILE_DEFINE(OpenGL_SurfaceFlush, "OpenGL", "Surface Flush", MP_RGB(128, 192, 64));
void CachedSurface::FlushGLBuffer() {
    MICROPROFILE_SCOPE(OpenGL_SurfaceFlush);

    ASSERT_MSG(!IsPixelFormatASTC(params.pixel_format), "Unimplemented");

    // OpenGL temporary buffer needs to be big enough to store raw texture size
    gl_buffer.resize(1);
    gl_buffer[0].resize(GetSizeInBytes());

    const FormatTuple& tuple = GetFormatTuple(params.pixel_format, params.component_type);
    // Ensure no bad interactions with GL_UNPACK_ALIGNMENT
    ASSERT(params.width * GetBytesPerPixel(params.pixel_format) % 4 == 0);
    glPixelStorei(GL_PACK_ROW_LENGTH, static_cast<GLint>(params.width));
    ASSERT(!tuple.compressed);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glGetTextureImage(texture.handle, 0, tuple.format, tuple.type,
                      static_cast<GLsizei>(gl_buffer[0].size()), gl_buffer[0].data());
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    ConvertFormatAsNeeded_FlushGLBuffer(gl_buffer[0], params.pixel_format, params.width,
                                        params.height);
    ASSERT(params.type != SurfaceType::Fill);
    const u8* const texture_src_data = Memory::GetPointer(params.addr);
    ASSERT(texture_src_data);
    if (params.is_tiled) {
        ASSERT_MSG(params.block_width == 1, "Block width is defined as {} on texture type {}",
                   params.block_width, static_cast<u32>(params.target));

        SwizzleFunc(gl_to_morton_fns, params, gl_buffer[0], 0);
    } else {
        std::memcpy(Memory::GetPointer(GetAddr()), gl_buffer[0].data(), GetSizeInBytes());
    }
}

void CachedSurface::UploadGLMipmapTexture(u32 mip_map, GLuint read_fb_handle,
                                          GLuint draw_fb_handle) {
    const auto& rect{params.GetRect(mip_map)};

    // Load data from memory to the surface
    const GLint x0 = static_cast<GLint>(rect.left);
    const GLint y0 = static_cast<GLint>(rect.bottom);
    std::size_t buffer_offset =
        static_cast<std::size_t>(static_cast<std::size_t>(y0) * params.MipWidth(mip_map) +
                                 static_cast<std::size_t>(x0)) *
        GetBytesPerPixel(params.pixel_format);

    const FormatTuple& tuple = GetFormatTuple(params.pixel_format, params.component_type);
    const GLuint target_tex = texture.handle;
    OpenGLState cur_state = OpenGLState::GetCurState();

    const auto& old_tex = cur_state.texture_units[0];
    SCOPE_EXIT({
        cur_state.texture_units[0] = old_tex;
        cur_state.Apply();
    });
    cur_state.texture_units[0].texture = target_tex;
    cur_state.texture_units[0].target = SurfaceTargetToGL(params.target);
    cur_state.Apply();

    // Ensure no bad interactions with GL_UNPACK_ALIGNMENT
    ASSERT(params.MipWidth(mip_map) * GetBytesPerPixel(params.pixel_format) % 4 == 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(params.MipWidth(mip_map)));

    GLsizei image_size = static_cast<GLsizei>(params.GetMipmapSizeGL(mip_map, false));
    glActiveTexture(GL_TEXTURE0);
    if (tuple.compressed) {
        switch (params.target) {
        case SurfaceTarget::Texture2D:
            glCompressedTexImage2D(SurfaceTargetToGL(params.target), mip_map, tuple.internal_format,
                                   static_cast<GLsizei>(params.MipWidth(mip_map)),
                                   static_cast<GLsizei>(params.MipHeight(mip_map)), 0, image_size,
                                   &gl_buffer[mip_map][buffer_offset]);
            break;
        case SurfaceTarget::Texture3D:
            glCompressedTexImage3D(SurfaceTargetToGL(params.target), mip_map, tuple.internal_format,
                                   static_cast<GLsizei>(params.MipWidth(mip_map)),
                                   static_cast<GLsizei>(params.MipHeight(mip_map)),
                                   static_cast<GLsizei>(params.MipDepth(mip_map)), 0, image_size,
                                   &gl_buffer[mip_map][buffer_offset]);
            break;
        case SurfaceTarget::Texture2DArray:
        case SurfaceTarget::TextureCubeArray:
            glCompressedTexImage3D(SurfaceTargetToGL(params.target), mip_map, tuple.internal_format,
                                   static_cast<GLsizei>(params.MipWidth(mip_map)),
                                   static_cast<GLsizei>(params.MipHeight(mip_map)),
                                   static_cast<GLsizei>(params.depth), 0, image_size,
                                   &gl_buffer[mip_map][buffer_offset]);
            break;
        case SurfaceTarget::TextureCubemap: {
            GLsizei layer_size = static_cast<GLsizei>(params.LayerSizeGL(mip_map));
            for (std::size_t face = 0; face < params.depth; ++face) {
                glCompressedTexImage2D(static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face),
                                       mip_map, tuple.internal_format,
                                       static_cast<GLsizei>(params.MipWidth(mip_map)),
                                       static_cast<GLsizei>(params.MipHeight(mip_map)), 0,
                                       layer_size, &gl_buffer[mip_map][buffer_offset]);
                buffer_offset += layer_size;
            }
            break;
        }
        default:
            LOG_CRITICAL(Render_OpenGL, "Unimplemented surface target={}",
                         static_cast<u32>(params.target));
            UNREACHABLE();
            glCompressedTexImage2D(GL_TEXTURE_2D, mip_map, tuple.internal_format,
                                   static_cast<GLsizei>(params.MipWidth(mip_map)),
                                   static_cast<GLsizei>(params.MipHeight(mip_map)), 0,
                                   static_cast<GLsizei>(params.size_in_bytes_gl),
                                   &gl_buffer[mip_map][buffer_offset]);
        }
    } else {

        switch (params.target) {
        case SurfaceTarget::Texture1D:
            glTexSubImage1D(SurfaceTargetToGL(params.target), mip_map, x0,
                            static_cast<GLsizei>(rect.GetWidth()), tuple.format, tuple.type,
                            &gl_buffer[mip_map][buffer_offset]);
            break;
        case SurfaceTarget::Texture2D:
            glTexSubImage2D(SurfaceTargetToGL(params.target), mip_map, x0, y0,
                            static_cast<GLsizei>(rect.GetWidth()),
                            static_cast<GLsizei>(rect.GetHeight()), tuple.format, tuple.type,
                            &gl_buffer[mip_map][buffer_offset]);
            break;
        case SurfaceTarget::Texture3D:
            glTexSubImage3D(SurfaceTargetToGL(params.target), mip_map, x0, y0, 0,
                            static_cast<GLsizei>(rect.GetWidth()),
                            static_cast<GLsizei>(rect.GetHeight()), params.MipDepth(mip_map),
                            tuple.format, tuple.type, &gl_buffer[mip_map][buffer_offset]);
            break;
        case SurfaceTarget::Texture2DArray:
        case SurfaceTarget::TextureCubeArray:
            glTexSubImage3D(SurfaceTargetToGL(params.target), mip_map, x0, y0, 0,
                            static_cast<GLsizei>(rect.GetWidth()),
                            static_cast<GLsizei>(rect.GetHeight()), params.depth, tuple.format,
                            tuple.type, &gl_buffer[mip_map][buffer_offset]);
            break;
        case SurfaceTarget::TextureCubemap: {
            std::size_t start = buffer_offset;
            for (std::size_t face = 0; face < params.depth; ++face) {
                glTexSubImage2D(static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), mip_map,
                                x0, y0, static_cast<GLsizei>(rect.GetWidth()),
                                static_cast<GLsizei>(rect.GetHeight()), tuple.format, tuple.type,
                                &gl_buffer[mip_map][buffer_offset]);
                buffer_offset += params.LayerSizeGL(mip_map);
            }
            break;
        }
        default:
            LOG_CRITICAL(Render_OpenGL, "Unimplemented surface target={}",
                         static_cast<u32>(params.target));
            UNREACHABLE();
            glTexSubImage2D(GL_TEXTURE_2D, mip_map, x0, y0, static_cast<GLsizei>(rect.GetWidth()),
                            static_cast<GLsizei>(rect.GetHeight()), tuple.format, tuple.type,
                            &gl_buffer[mip_map][buffer_offset]);
        }
    }

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

MICROPROFILE_DEFINE(OpenGL_TextureUL, "OpenGL", "Texture Upload", MP_RGB(128, 192, 64));
void CachedSurface::UploadGLTexture(GLuint read_fb_handle, GLuint draw_fb_handle) {
    if (params.type == SurfaceType::Fill)
        return;

    MICROPROFILE_SCOPE(OpenGL_TextureUL);

    for (u32 i = 0; i < params.max_mip_level; i++)
        UploadGLMipmapTexture(i, read_fb_handle, draw_fb_handle);
}

RasterizerCacheOpenGL::RasterizerCacheOpenGL(RasterizerOpenGL& rasterizer)
    : RasterizerCache{rasterizer} {
    read_framebuffer.Create();
    draw_framebuffer.Create();
    copy_pbo.Create();
}

Surface RasterizerCacheOpenGL::GetTextureSurface(const Tegra::Texture::FullTextureInfo& config,
                                                 const GLShader::SamplerEntry& entry) {
    return GetSurface(SurfaceParams::CreateForTexture(config, entry));
}

Surface RasterizerCacheOpenGL::GetDepthBufferSurface(bool preserve_contents) {
    const auto& regs{Core::System::GetInstance().GPU().Maxwell3D().regs};
    if (!regs.zeta.Address() || !regs.zeta_enable) {
        return {};
    }

    SurfaceParams depth_params{SurfaceParams::CreateForDepthBuffer(
        regs.zeta_width, regs.zeta_height, regs.zeta.Address(), regs.zeta.format,
        regs.zeta.memory_layout.block_width, regs.zeta.memory_layout.block_height,
        regs.zeta.memory_layout.block_depth, regs.zeta.memory_layout.type)};

    return GetSurface(depth_params, preserve_contents);
}

Surface RasterizerCacheOpenGL::GetColorBufferSurface(std::size_t index, bool preserve_contents) {
    const auto& regs{Core::System::GetInstance().GPU().Maxwell3D().regs};

    ASSERT(index < Tegra::Engines::Maxwell3D::Regs::NumRenderTargets);

    if (index >= regs.rt_control.count) {
        return {};
    }

    if (regs.rt[index].Address() == 0 || regs.rt[index].format == Tegra::RenderTargetFormat::NONE) {
        return {};
    }

    const SurfaceParams color_params{SurfaceParams::CreateForFramebuffer(index)};

    return GetSurface(color_params, preserve_contents);
}

void RasterizerCacheOpenGL::LoadSurface(const Surface& surface) {
    surface->LoadGLBuffer();
    surface->UploadGLTexture(read_framebuffer.handle, draw_framebuffer.handle);
    surface->MarkAsModified(false, *this);
}

Surface RasterizerCacheOpenGL::GetSurface(const SurfaceParams& params, bool preserve_contents) {
    if (params.addr == 0 || params.height * params.width == 0) {
        return {};
    }

    // Look up surface in the cache based on address
    Surface surface{TryGet(params.addr)};
    if (surface) {
        if (surface->GetSurfaceParams().IsCompatibleSurface(params)) {
            // Use the cached surface as-is
            return surface;
        } else if (preserve_contents) {
            // If surface parameters changed and we care about keeping the previous data, recreate
            // the surface from the old one
            Surface new_surface{RecreateSurface(surface, params)};
            Unregister(surface);
            Register(new_surface);
            return new_surface;
        } else {
            // Delete the old surface before creating a new one to prevent collisions.
            Unregister(surface);
        }
    }

    // No cached surface found - get a new one
    surface = GetUncachedSurface(params);
    Register(surface);

    // Only load surface from memory if we care about the contents
    if (preserve_contents) {
        LoadSurface(surface);
    }

    return surface;
}

Surface RasterizerCacheOpenGL::GetUncachedSurface(const SurfaceParams& params) {
    Surface surface{TryGetReservedSurface(params)};
    if (!surface) {
        // No reserved surface available, create a new one and reserve it
        surface = std::make_shared<CachedSurface>(params);
        ReserveSurface(surface);
    }
    return surface;
}

void RasterizerCacheOpenGL::FermiCopySurface(
    const Tegra::Engines::Fermi2D::Regs::Surface& src_config,
    const Tegra::Engines::Fermi2D::Regs::Surface& dst_config) {

    const auto& src_params = SurfaceParams::CreateForFermiCopySurface(src_config);
    const auto& dst_params = SurfaceParams::CreateForFermiCopySurface(dst_config);

    ASSERT(src_params.width == dst_params.width);
    ASSERT(src_params.height == dst_params.height);
    ASSERT(src_params.pixel_format == dst_params.pixel_format);
    ASSERT(src_params.block_height == dst_params.block_height);
    ASSERT(src_params.is_tiled == dst_params.is_tiled);
    ASSERT(src_params.depth == dst_params.depth);
    ASSERT(src_params.depth == 1); // Currently, FastCopySurface only works with 2D surfaces
    ASSERT(src_params.target == dst_params.target);
    ASSERT(src_params.rt.index == dst_params.rt.index);

    FastCopySurface(GetSurface(src_params, true), GetSurface(dst_params, false));
}

void RasterizerCacheOpenGL::AccurateCopySurface(const Surface& src_surface,
                                                const Surface& dst_surface) {
    const auto& src_params{src_surface->GetSurfaceParams()};
    const auto& dst_params{dst_surface->GetSurfaceParams()};
    FlushRegion(src_params.addr, dst_params.MemorySize());
    LoadSurface(dst_surface);
}

Surface RasterizerCacheOpenGL::RecreateSurface(const Surface& old_surface,
                                               const SurfaceParams& new_params) {
    // Verify surface is compatible for blitting
    auto old_params{old_surface->GetSurfaceParams()};

    // Get a new surface with the new parameters, and blit the previous surface to it
    Surface new_surface{GetUncachedSurface(new_params)};

    // With use_accurate_gpu_emulation enabled, do an accurate surface copy
    if (Settings::values.use_accurate_gpu_emulation) {
        AccurateCopySurface(old_surface, new_surface);
        return new_surface;
    }

    // For compatible surfaces, we can just do fast glCopyImageSubData based copy
    if (old_params.target == new_params.target && old_params.type == new_params.type &&
        old_params.depth == new_params.depth && old_params.depth == 1 &&
        GetFormatBpp(old_params.pixel_format) == GetFormatBpp(new_params.pixel_format)) {
        FastCopySurface(old_surface, new_surface);
        return new_surface;
    }

    // If the format is the same, just do a framebuffer blit. This is significantly faster than
    // using PBOs. The is also likely less accurate, as textures will be converted rather than
    // reinterpreted. When use_accurate_gpu_emulation setting is enabled, perform a more accurate
    // surface copy, where pixels are reinterpreted as a new format (without conversion). This
    // code path uses OpenGL PBOs and is quite slow.
    const bool is_blit{old_params.pixel_format == new_params.pixel_format};

    switch (new_params.target) {
    case SurfaceTarget::Texture2D:
        if (is_blit) {
            BlitSurface(old_surface, new_surface, read_framebuffer.handle, draw_framebuffer.handle);
        } else {
            CopySurface(old_surface, new_surface, copy_pbo.handle);
        }
        break;
    case SurfaceTarget::TextureCubemap:
    case SurfaceTarget::Texture3D:
    case SurfaceTarget::Texture2DArray:
    case SurfaceTarget::TextureCubeArray:
        AccurateCopySurface(old_surface, new_surface);
        break;
    default:
        LOG_CRITICAL(Render_OpenGL, "Unimplemented surface target={}",
                     static_cast<u32>(new_params.target));
        UNREACHABLE();
    }

    return new_surface;
}

Surface RasterizerCacheOpenGL::TryFindFramebufferSurface(VAddr addr) const {
    return TryGet(addr);
}

void RasterizerCacheOpenGL::ReserveSurface(const Surface& surface) {
    const auto& surface_reserve_key{SurfaceReserveKey::Create(surface->GetSurfaceParams())};
    surface_reserve[surface_reserve_key] = surface;
}

Surface RasterizerCacheOpenGL::TryGetReservedSurface(const SurfaceParams& params) {
    const auto& surface_reserve_key{SurfaceReserveKey::Create(params)};
    auto search{surface_reserve.find(surface_reserve_key)};
    if (search != surface_reserve.end()) {
        return search->second;
    }
    return {};
}

} // namespace OpenGL
