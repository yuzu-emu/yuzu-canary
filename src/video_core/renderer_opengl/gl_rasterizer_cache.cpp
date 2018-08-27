// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <glad/glad.h>

#include "common/alignment.h"
#include "common/assert.h"
#include "common/microprofile.h"
#include "common/scope_exit.h"
#include "core/core.h"
#include "core/hle/kernel/process.h"
#include "core/memory.h"
#include "core/settings.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_opengl/gl_rasterizer_cache.h"
#include "video_core/textures/astc.h"
#include "video_core/textures/decoders.h"
#include "video_core/utils.h"

namespace OpenGL {

using SurfaceType = SurfaceParams::SurfaceType;
using PixelFormat = SurfaceParams::PixelFormat;
using ComponentType = SurfaceParams::ComponentType;

struct FormatTuple {
    GLint internal_format;
    GLenum format;
    GLenum type;
    ComponentType component_type;
    bool compressed;
};

/*static*/ SurfaceParams SurfaceParams::CreateForTexture(
    const Tegra::Texture::FullTextureInfo& config) {

    SurfaceParams params{};
    params.addr = config.tic.Address();
    params.is_tiled = config.tic.IsTiled();
    params.block_height = params.is_tiled ? config.tic.BlockHeight() : 0,
    params.pixel_format =
        PixelFormatFromTextureFormat(config.tic.format, config.tic.r_type.Value());
    params.component_type = ComponentTypeFromTexture(config.tic.r_type.Value());
    params.type = GetFormatType(params.pixel_format);
    params.width = Common::AlignUp(config.tic.Width(), GetCompressionFactor(params.pixel_format));
    params.height = Common::AlignUp(config.tic.Height(), GetCompressionFactor(params.pixel_format));
    params.unaligned_height = config.tic.Height();
    params.size_in_bytes = params.SizeInBytes();
    params.cache_width = Common::AlignUp(params.width, 16);
    params.cache_height = Common::AlignUp(params.height, 16);
    return params;
}

/*static*/ SurfaceParams SurfaceParams::CreateForFramebuffer(
    const Tegra::Engines::Maxwell3D::Regs::RenderTargetConfig& config) {

    SurfaceParams params{};
    params.addr = config.Address();
    params.is_tiled = true;
    params.block_height = Tegra::Texture::TICEntry::DefaultBlockHeight;
    params.pixel_format = PixelFormatFromRenderTargetFormat(config.format);
    params.component_type = ComponentTypeFromRenderTarget(config.format);
    params.type = GetFormatType(params.pixel_format);
    params.width = config.width;
    params.height = config.height;
    params.unaligned_height = config.height;
    params.size_in_bytes = params.SizeInBytes();
    params.cache_width = Common::AlignUp(params.width, 16);
    params.cache_height = Common::AlignUp(params.height, 16);
    return params;
}

/*static*/ SurfaceParams SurfaceParams::CreateForDepthBuffer(u32 zeta_width, u32 zeta_height,
                                                             Tegra::GPUVAddr zeta_address,
                                                             Tegra::DepthFormat format) {

    SurfaceParams params{};
    params.addr = zeta_address;
    params.is_tiled = true;
    params.block_height = Tegra::Texture::TICEntry::DefaultBlockHeight;
    params.pixel_format = PixelFormatFromDepthFormat(format);
    params.component_type = ComponentTypeFromDepthFormat(format);
    params.type = GetFormatType(params.pixel_format);
    params.size_in_bytes = params.SizeInBytes();
    params.width = zeta_width;
    params.height = zeta_height;
    params.unaligned_height = zeta_height;
    params.size_in_bytes = params.SizeInBytes();
    params.cache_width = Common::AlignUp(params.width, 16);
    params.cache_height = Common::AlignUp(params.height, 16);
    return params;
}

static constexpr std::array<FormatTuple, SurfaceParams::MaxPixelFormat> tex_format_tuples = {{
    {GL_RGBA8, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, ComponentType::UNorm, false}, // ABGR8U
    {GL_RGBA8, GL_RGBA, GL_BYTE, ComponentType::SNorm, false},                     // ABGR8S
    {GL_RGBA8UI, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, ComponentType::UInt, false},   // ABGR8UI
    {GL_RGB, GL_RGB, GL_UNSIGNED_SHORT_5_6_5_REV, ComponentType::UNorm, false},    // B5G6R5U
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
    {GL_COMPRESSED_RGB_S3TC_DXT1_EXT, GL_RGB, GL_UNSIGNED_INT_8_8_8_8, ComponentType::UNorm,
     true}, // DXT1
    {GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, ComponentType::UNorm,
     true}, // DXT23
    {GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, ComponentType::UNorm,
     true},                                                                                 // DXT45
    {GL_COMPRESSED_RED_RGTC1, GL_RED, GL_UNSIGNED_INT_8_8_8_8, ComponentType::UNorm, true}, // DXN1
    {GL_COMPRESSED_RG_RGTC2, GL_RG, GL_UNSIGNED_INT_8_8_8_8, ComponentType::UNorm,
     true},                                                                     // DXN2UNORM
    {GL_COMPRESSED_SIGNED_RG_RGTC2, GL_RG, GL_INT, ComponentType::SNorm, true}, // DXN2SNORM
    {GL_COMPRESSED_RGBA_BPTC_UNORM_ARB, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, ComponentType::UNorm,
     true},                                                                    // BC7U
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
    {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, ComponentType::UNorm, false}, // SRGBA8
    {GL_RG8, GL_RG, GL_UNSIGNED_BYTE, ComponentType::UNorm, false},                       // RG8U
    {GL_RG8, GL_RG, GL_BYTE, ComponentType::SNorm, false},                                // RG8S
    {GL_RG32UI, GL_RG_INTEGER, GL_UNSIGNED_INT, ComponentType::UInt, false},              // RG32UI
    {GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, ComponentType::UInt, false},              // R32UI

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

static const FormatTuple& GetFormatTuple(PixelFormat pixel_format, ComponentType component_type) {
    ASSERT(static_cast<size_t>(pixel_format) < tex_format_tuples.size());
    auto& format = tex_format_tuples[static_cast<unsigned int>(pixel_format)];
    ASSERT(component_type == format.component_type);

    return format;
}

VAddr SurfaceParams::GetCpuAddr() const {
    const auto& gpu = Core::System::GetInstance().GPU();
    return *gpu.memory_manager->GpuToCpuAddress(addr);
}

static bool IsPixelFormatASTC(PixelFormat format) {
    switch (format) {
    case PixelFormat::ASTC_2D_4X4:
        return true;
    default:
        return false;
    }
}

static std::pair<u32, u32> GetASTCBlockSize(PixelFormat format) {
    switch (format) {
    case PixelFormat::ASTC_2D_4X4:
        return {4, 4};
    default:
        LOG_CRITICAL(HW_GPU, "Unhandled format: {}", static_cast<u32>(format));
        UNREACHABLE();
    }
}

MathUtil::Rectangle<u32> SurfaceParams::GetRect() const {
    u32 actual_height{unaligned_height};
    if (IsPixelFormatASTC(pixel_format)) {
        // ASTC formats must stop at the ATSC block size boundary
        actual_height = Common::AlignDown(actual_height, GetASTCBlockSize(pixel_format).second);
    }
    return {0, actual_height, width, 0};
}

/// Returns true if the specified PixelFormat is a BCn format, e.g. DXT or DXN
static bool IsFormatBCn(PixelFormat format) {
    switch (format) {
    case PixelFormat::DXT1:
    case PixelFormat::DXT23:
    case PixelFormat::DXT45:
    case PixelFormat::DXN1:
    case PixelFormat::DXN2SNORM:
    case PixelFormat::DXN2UNORM:
    case PixelFormat::BC7U:
        return true;
    }
    return false;
}

template <bool morton_to_gl, PixelFormat format>
void MortonCopy(u32 stride, u32 block_height, u32 height, std::vector<u8>& gl_buffer,
                Tegra::GPUVAddr addr) {
    constexpr u32 bytes_per_pixel = SurfaceParams::GetFormatBpp(format) / CHAR_BIT;
    constexpr u32 gl_bytes_per_pixel = CachedSurface::GetGLBytesPerPixel(format);
    const auto& gpu = Core::System::GetInstance().GPU();

    if (morton_to_gl) {
        // With the BCn formats (DXT and DXN), each 4x4 tile is swizzled instead of just individual
        // pixel values.
        const u32 tile_size{IsFormatBCn(format) ? 4U : 1U};
        const std::vector<u8> data =
            Tegra::Texture::UnswizzleTexture(*gpu.memory_manager->GpuToCpuAddress(addr), tile_size,
                                             bytes_per_pixel, stride, height, block_height);
        const size_t size_to_copy{std::min(gl_buffer.size(), data.size())};
        gl_buffer.assign(data.begin(), data.begin() + size_to_copy);
    } else {
        // TODO(bunnei): Assumes the default rendering GOB size of 16 (128 lines). We should
        // check the configuration for this and perform more generic un/swizzle
        LOG_WARNING(Render_OpenGL, "need to use correct swizzle/GOB parameters!");
        VideoCore::MortonCopyPixels128(
            stride, height, bytes_per_pixel, gl_bytes_per_pixel,
            Memory::GetPointer(*gpu.memory_manager->GpuToCpuAddress(addr)), gl_buffer.data(),
            morton_to_gl);
    }
}

static constexpr std::array<void (*)(u32, u32, u32, std::vector<u8>&, Tegra::GPUVAddr),
                            SurfaceParams::MaxPixelFormat>
    morton_to_gl_fns = {
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
        MortonCopy<true, PixelFormat::SRGBA8>,
        MortonCopy<true, PixelFormat::RG8U>,
        MortonCopy<true, PixelFormat::RG8S>,
        MortonCopy<true, PixelFormat::RG32UI>,
        MortonCopy<true, PixelFormat::R32UI>,
        MortonCopy<true, PixelFormat::Z32F>,
        MortonCopy<true, PixelFormat::Z16>,
        MortonCopy<true, PixelFormat::Z24S8>,
        MortonCopy<true, PixelFormat::S8Z24>,
        MortonCopy<true, PixelFormat::Z32FS8>,
        // clang-format on
};

static constexpr std::array<void (*)(u32, u32, u32, std::vector<u8>&, Tegra::GPUVAddr),
                            SurfaceParams::MaxPixelFormat>
    gl_to_morton_fns = {
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
        // TODO(Subv): Swizzling DXT1/DXT23/DXT45/DXN1/DXN2/BC7U/ASTC_2D_4X4 formats is not
        // supported
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
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
        MortonCopy<false, PixelFormat::SRGBA8>,
        MortonCopy<false, PixelFormat::RG8U>,
        MortonCopy<false, PixelFormat::RG8S>,
        MortonCopy<false, PixelFormat::RG32UI>,
        MortonCopy<false, PixelFormat::R32UI>,
        MortonCopy<false, PixelFormat::Z32F>,
        MortonCopy<false, PixelFormat::Z16>,
        MortonCopy<false, PixelFormat::Z24S8>,
        MortonCopy<false, PixelFormat::S8Z24>,
        MortonCopy<false, PixelFormat::Z32FS8>,
        // clang-format on
};

// Allocate an uninitialized texture of appropriate size and format for the surface
static void AllocateSurfaceTexture(GLuint texture, const FormatTuple& format_tuple, u32 width,
                                   u32 height) {
    OpenGLState cur_state = OpenGLState::GetCurState();

    // Keep track of previous texture bindings
    GLuint old_tex = cur_state.texture_units[0].texture_2d;
    cur_state.texture_units[0].texture_2d = texture;
    cur_state.Apply();
    glActiveTexture(GL_TEXTURE0);

    if (!format_tuple.compressed) {
        // Only pre-create the texture for non-compressed textures.
        glTexImage2D(GL_TEXTURE_2D, 0, format_tuple.internal_format, width, height, 0,
                     format_tuple.format, format_tuple.type, nullptr);
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Restore previous texture bindings
    cur_state.texture_units[0].texture_2d = old_tex;
    cur_state.Apply();
}

static bool BlitTextures(GLuint src_tex, const MathUtil::Rectangle<u32>& src_rect, GLuint dst_tex,
                         const MathUtil::Rectangle<u32>& dst_rect, SurfaceType type,
                         GLuint read_fb_handle, GLuint draw_fb_handle) {
    OpenGLState prev_state{OpenGLState::GetCurState()};
    SCOPE_EXIT({ prev_state.Apply(); });

    OpenGLState state;
    state.draw.read_framebuffer = read_fb_handle;
    state.draw.draw_framebuffer = draw_fb_handle;
    state.Apply();

    u32 buffers{};

    if (type == SurfaceType::ColorTexture) {
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, src_tex,
                               0);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0,
                               0);

        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst_tex,
                               0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0,
                               0);

        buffers = GL_COLOR_BUFFER_BIT;
    } else if (type == SurfaceType::Depth) {
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, src_tex, 0);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);

        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, dst_tex, 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);

        buffers = GL_DEPTH_BUFFER_BIT;
    } else if (type == SurfaceType::DepthStencil) {
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                               src_tex, 0);

        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                               dst_tex, 0);

        buffers = GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
    }

    glBlitFramebuffer(src_rect.left, src_rect.bottom, src_rect.right, src_rect.top, dst_rect.left,
                      dst_rect.bottom, dst_rect.right, dst_rect.top, buffers,
                      buffers == GL_COLOR_BUFFER_BIT ? GL_LINEAR : GL_NEAREST);

    return true;
}

CachedSurface::CachedSurface(const SurfaceParams& params) : params(params) {
    texture.Create();
    const auto& rect{params.GetRect()};
    AllocateSurfaceTexture(texture.handle,
                           GetFormatTuple(params.pixel_format, params.component_type),
                           rect.GetWidth(), rect.GetHeight());
}

static void ConvertS8Z24ToZ24S8(std::vector<u8>& data, u32 width, u32 height) {
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

    S8Z24 input_pixel{};
    Z24S8 output_pixel{};
    const auto bpp{CachedSurface::GetGLBytesPerPixel(PixelFormat::S8Z24)};
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            const size_t offset{bpp * (y * width + x)};
            std::memcpy(&input_pixel, &data[offset], sizeof(S8Z24));
            output_pixel.s8.Assign(input_pixel.s8);
            output_pixel.z24.Assign(input_pixel.z24);
            std::memcpy(&data[offset], &output_pixel, sizeof(Z24S8));
        }
    }
}

static void ConvertG8R8ToR8G8(std::vector<u8>& data, u32 width, u32 height) {
    const auto bpp{CachedSurface::GetGLBytesPerPixel(PixelFormat::G8R8U)};
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            const size_t offset{bpp * (y * width + x)};
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
                                               u32 width, u32 height) {
    switch (pixel_format) {
    case PixelFormat::ASTC_2D_4X4: {
        // Convert ASTC pixel formats to RGBA8, as most desktop GPUs do not support ASTC.
        u32 block_width{};
        u32 block_height{};
        std::tie(block_width, block_height) = GetASTCBlockSize(pixel_format);
        data = Tegra::Texture::ASTC::Decompress(data, width, height, block_width, block_height);
        break;
    }
    case PixelFormat::S8Z24:
        // Convert the S8Z24 depth format to Z24S8, as OpenGL does not support S8Z24.
        ConvertS8Z24ToZ24S8(data, width, height);
        break;

    case PixelFormat::G8R8U:
    case PixelFormat::G8R8S:
        // Convert the G8R8 color format to R8G8, as OpenGL does not support G8R8.
        ConvertG8R8ToR8G8(data, width, height);
        break;
    }
}

/**
 * Helper function to perform software conversion (as needed) when flushing a buffer to Switch
 * memory. This is for Maxwell pixel formats that cannot be represented as-is in OpenGL or with
 * typical desktop GPUs.
 */
static void ConvertFormatAsNeeded_FlushGLBuffer(std::vector<u8>& /*data*/, PixelFormat pixel_format,
                                                u32 /*width*/, u32 /*height*/) {
    switch (pixel_format) {
    case PixelFormat::ASTC_2D_4X4:
    case PixelFormat::S8Z24:
        LOG_CRITICAL(Render_OpenGL, "Unimplemented pixel_format={}",
                     static_cast<u32>(pixel_format));
        UNREACHABLE();
        break;
    }
}

MICROPROFILE_DEFINE(OpenGL_SurfaceLoad, "OpenGL", "Surface Load", MP_RGB(128, 64, 192));
void CachedSurface::LoadGLBuffer() {
    ASSERT(params.type != SurfaceType::Fill);

    const u8* const texture_src_data = Memory::GetPointer(params.GetCpuAddr());

    ASSERT(texture_src_data);

    const u32 bytes_per_pixel = GetGLBytesPerPixel(params.pixel_format);
    const u32 copy_size = params.width * params.height * bytes_per_pixel;

    MICROPROFILE_SCOPE(OpenGL_SurfaceLoad);

    if (params.is_tiled) {
        gl_buffer.resize(copy_size);

        morton_to_gl_fns[static_cast<size_t>(params.pixel_format)](
            params.width, params.block_height, params.height, gl_buffer, params.addr);
    } else {
        const u8* const texture_src_data_end = texture_src_data + copy_size;

        gl_buffer.assign(texture_src_data, texture_src_data_end);
    }

    ConvertFormatAsNeeded_LoadGLBuffer(gl_buffer, params.pixel_format, params.width, params.height);
}

MICROPROFILE_DEFINE(OpenGL_SurfaceFlush, "OpenGL", "Surface Flush", MP_RGB(128, 192, 64));
void CachedSurface::FlushGLBuffer() {
    u8* const dst_buffer = Memory::GetPointer(params.GetCpuAddr());

    ASSERT(dst_buffer);
    ASSERT(gl_buffer.size() ==
           params.width * params.height * GetGLBytesPerPixel(params.pixel_format));

    MICROPROFILE_SCOPE(OpenGL_SurfaceFlush);

    ConvertFormatAsNeeded_FlushGLBuffer(gl_buffer, params.pixel_format, params.width,
                                        params.height);

    if (!params.is_tiled) {
        std::memcpy(dst_buffer, gl_buffer.data(), params.size_in_bytes);
    } else {
        gl_to_morton_fns[static_cast<size_t>(params.pixel_format)](
            params.width, params.block_height, params.height, gl_buffer, params.addr);
    }
}

MICROPROFILE_DEFINE(OpenGL_TextureUL, "OpenGL", "Texture Upload", MP_RGB(128, 64, 192));
void CachedSurface::UploadGLTexture(GLuint read_fb_handle, GLuint draw_fb_handle) {
    if (params.type == SurfaceType::Fill)
        return;

    MICROPROFILE_SCOPE(OpenGL_TextureUL);

    ASSERT(gl_buffer.size() ==
           params.width * params.height * GetGLBytesPerPixel(params.pixel_format));

    const auto& rect{params.GetRect()};

    // Load data from memory to the surface
    GLint x0 = static_cast<GLint>(rect.left);
    GLint y0 = static_cast<GLint>(rect.bottom);
    size_t buffer_offset = (y0 * params.width + x0) * GetGLBytesPerPixel(params.pixel_format);

    const FormatTuple& tuple = GetFormatTuple(params.pixel_format, params.component_type);
    GLuint target_tex = texture.handle;
    OpenGLState cur_state = OpenGLState::GetCurState();

    GLuint old_tex = cur_state.texture_units[0].texture_2d;
    cur_state.texture_units[0].texture_2d = target_tex;
    cur_state.Apply();

    // Ensure no bad interactions with GL_UNPACK_ALIGNMENT
    ASSERT(params.width * GetGLBytesPerPixel(params.pixel_format) % 4 == 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(params.width));

    glActiveTexture(GL_TEXTURE0);
    if (tuple.compressed) {
        glCompressedTexImage2D(
            GL_TEXTURE_2D, 0, tuple.internal_format, static_cast<GLsizei>(params.width),
            static_cast<GLsizei>(params.height), 0, static_cast<GLsizei>(params.size_in_bytes),
            &gl_buffer[buffer_offset]);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, x0, y0, static_cast<GLsizei>(rect.GetWidth()),
                        static_cast<GLsizei>(rect.GetHeight()), tuple.format, tuple.type,
                        &gl_buffer[buffer_offset]);
    }

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    cur_state.texture_units[0].texture_2d = old_tex;
    cur_state.Apply();
}

MICROPROFILE_DEFINE(OpenGL_TextureDL, "OpenGL", "Texture Download", MP_RGB(128, 192, 64));
void CachedSurface::DownloadGLTexture(GLuint read_fb_handle, GLuint draw_fb_handle) {
    if (params.type == SurfaceType::Fill)
        return;

    MICROPROFILE_SCOPE(OpenGL_TextureDL);

    gl_buffer.resize(params.width * params.height * GetGLBytesPerPixel(params.pixel_format));

    OpenGLState state = OpenGLState::GetCurState();
    OpenGLState prev_state = state;
    SCOPE_EXIT({ prev_state.Apply(); });

    const FormatTuple& tuple = GetFormatTuple(params.pixel_format, params.component_type);

    // Ensure no bad interactions with GL_PACK_ALIGNMENT
    ASSERT(params.width * GetGLBytesPerPixel(params.pixel_format) % 4 == 0);
    glPixelStorei(GL_PACK_ROW_LENGTH, static_cast<GLint>(params.width));

    const auto& rect{params.GetRect()};
    size_t buffer_offset =
        (rect.bottom * params.width + rect.left) * GetGLBytesPerPixel(params.pixel_format);

    state.UnbindTexture(texture.handle);
    state.draw.read_framebuffer = read_fb_handle;
    state.Apply();

    if (params.type == SurfaceType::ColorTexture) {
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               texture.handle, 0);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0,
                               0);
    } else if (params.type == SurfaceType::Depth) {
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                               texture.handle, 0);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
    } else {
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                               texture.handle, 0);
    }
    glReadPixels(static_cast<GLint>(rect.left), static_cast<GLint>(rect.bottom),
                 static_cast<GLsizei>(rect.GetWidth()), static_cast<GLsizei>(rect.GetHeight()),
                 tuple.format, tuple.type, &gl_buffer[buffer_offset]);

    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
}

RasterizerCacheOpenGL::RasterizerCacheOpenGL() {
    read_framebuffer.Create();
    draw_framebuffer.Create();
}

Surface RasterizerCacheOpenGL::GetTextureSurface(const Tegra::Texture::FullTextureInfo& config) {
    return GetSurface(SurfaceParams::CreateForTexture(config));
}

SurfaceSurfaceRect_Tuple RasterizerCacheOpenGL::GetFramebufferSurfaces(bool using_color_fb,
                                                                       bool using_depth_fb,
                                                                       bool preserve_contents) {
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;

    // TODO(bunnei): This is hard corded to use just the first render buffer
    LOG_WARNING(Render_OpenGL, "hard-coded for render target 0!");

    // get color and depth surfaces
    SurfaceParams color_params{};
    SurfaceParams depth_params{};

    if (using_color_fb) {
        color_params = SurfaceParams::CreateForFramebuffer(regs.rt[0]);
    }

    if (using_depth_fb) {
        depth_params = SurfaceParams::CreateForDepthBuffer(regs.zeta_width, regs.zeta_height,
                                                           regs.zeta.Address(), regs.zeta.format);
    }

    MathUtil::Rectangle<u32> color_rect{};
    Surface color_surface;
    if (using_color_fb) {
        color_surface = GetSurface(color_params, preserve_contents);
        if (color_surface) {
            color_rect = color_surface->GetSurfaceParams().GetRect();
        }
    }

    MathUtil::Rectangle<u32> depth_rect{};
    Surface depth_surface;
    if (using_depth_fb) {
        depth_surface = GetSurface(depth_params, preserve_contents);
        if (depth_surface) {
            depth_rect = depth_surface->GetSurfaceParams().GetRect();
        }
    }

    MathUtil::Rectangle<u32> fb_rect{};
    if (color_surface && depth_surface) {
        fb_rect = color_rect;
        // Color and Depth surfaces must have the same dimensions and offsets
        if (color_rect.bottom != depth_rect.bottom || color_rect.top != depth_rect.top ||
            color_rect.left != depth_rect.left || color_rect.right != depth_rect.right) {
            color_surface = GetSurface(color_params);
            depth_surface = GetSurface(depth_params);
            fb_rect = color_surface->GetSurfaceParams().GetRect();
        }
    } else if (color_surface) {
        fb_rect = color_rect;
    } else if (depth_surface) {
        fb_rect = depth_rect;
    }

    return std::make_tuple(color_surface, depth_surface, fb_rect);
}

void RasterizerCacheOpenGL::LoadSurface(const Surface& surface) {
    surface->LoadGLBuffer();
    surface->UploadGLTexture(read_framebuffer.handle, draw_framebuffer.handle);
}

void RasterizerCacheOpenGL::FlushSurface(const Surface& surface) {
    surface->DownloadGLTexture(read_framebuffer.handle, draw_framebuffer.handle);
    surface->FlushGLBuffer();
}

Surface RasterizerCacheOpenGL::GetSurface(const SurfaceParams& params, bool preserve_contents) {
    if (params.addr == 0 || params.height * params.width == 0) {
        return {};
    }

    const auto& gpu = Core::System::GetInstance().GPU();
    // Don't try to create any entries in the cache if the address of the texture is invalid.
    if (gpu.memory_manager->GpuToCpuAddress(params.addr) == boost::none)
        return {};

    // Look up surface in the cache based on address
    Surface surface{TryGet(params.addr)};
    if (surface) {
        if (Settings::values.use_accurate_framebuffers) {
            // If use_accurate_framebuffers is enabled, always load from memory
            FlushSurface(surface);
            Unregister(surface);
        } else if (surface->GetSurfaceParams().IsCompatibleSurface(params)) {
            // Use the cached surface as-is
            return surface;
        } else if (preserve_contents) {
            // If surface parameters changed and we care about keeping the previous data, recreate
            // the surface from the old one
            Unregister(surface);
            Surface new_surface{RecreateSurface(surface, params)};
            Register(new_surface);
            return new_surface;
        } else {
            // Delete the old surface before creating a new one to prevent collisions.
            Unregister(surface);
        }
    }

    // Try to get a previously reserved surface
    surface = TryGetReservedSurface(params);

    // No surface found - create a new one
    if (!surface) {
        surface = std::make_shared<CachedSurface>(params);
        ReserveSurface(surface);
        Register(surface);
    }

    // Only load surface from memory if we care about the contents
    if (preserve_contents) {
        LoadSurface(surface);
    }

    return surface;
}

Surface RasterizerCacheOpenGL::RecreateSurface(const Surface& surface,
                                               const SurfaceParams& new_params) {
    // Verify surface is compatible for blitting
    const auto& params{surface->GetSurfaceParams()};

    // Create a new surface with the new parameters, and blit the previous surface to it
    Surface new_surface{std::make_shared<CachedSurface>(new_params)};

    // If format is unchanged, we can do a faster blit without reinterpreting pixel data
    if (params.pixel_format == new_params.pixel_format) {
        BlitTextures(surface->Texture().handle, params.GetRect(), new_surface->Texture().handle,
                     new_surface->GetSurfaceParams().GetRect(), params.type,
                     read_framebuffer.handle, draw_framebuffer.handle);
        return new_surface;
    }

    auto source_format = GetFormatTuple(params.pixel_format, params.component_type);
    auto dest_format = GetFormatTuple(new_params.pixel_format, new_params.component_type);

    size_t buffer_size = std::max(params.SizeInBytes(), new_params.SizeInBytes());

    // Use a Pixel Buffer Object to download the previous texture and then upload it to the new one
    // using the new format.
    OGLBuffer pbo;
    pbo.Create();

    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo.handle);
    glBufferData(GL_PIXEL_PACK_BUFFER, buffer_size, nullptr, GL_STREAM_DRAW_ARB);
    if (source_format.compressed) {
        glGetCompressedTextureImage(surface->Texture().handle, 0,
                                    static_cast<GLsizei>(params.SizeInBytes()), nullptr);
    } else {
        glGetTextureImage(surface->Texture().handle, 0, source_format.format, source_format.type,
                          static_cast<GLsizei>(params.SizeInBytes()), nullptr);
    }
    // If the new texture is bigger than the previous one, we need to fill in the rest with data
    // from the CPU.
    if (params.SizeInBytes() < new_params.SizeInBytes()) {
        // Upload the rest of the memory.
        if (new_params.is_tiled) {
            // TODO(Subv): We might have to de-tile the subtexture and re-tile it with the rest of
            // the data in this case. Games like Super Mario Odyssey seem to hit this case when
            // drawing, it re-uses the memory of a previous texture as a bigger framebuffer but it
            // doesn't clear it beforehand, the texture is already full of zeros.
            LOG_CRITICAL(HW_GPU, "Trying to upload extra texture data from the CPU during "
                                 "reinterpretation but the texture is tiled.");
        }
        size_t remaining_size = new_params.SizeInBytes() - params.SizeInBytes();
        auto address = Core::System::GetInstance().GPU().memory_manager->GpuToCpuAddress(
            new_params.addr + params.SizeInBytes());
        std::vector<u8> data(remaining_size);
        Memory::ReadBlock(*address, data.data(), data.size());
        glBufferSubData(GL_PIXEL_PACK_BUFFER, params.SizeInBytes(), remaining_size, data.data());
    }

    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    const auto& dest_rect{new_params.GetRect()};

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo.handle);
    if (dest_format.compressed) {
        glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                  static_cast<GLsizei>(dest_rect.GetWidth()),
                                  static_cast<GLsizei>(dest_rect.GetHeight()), dest_format.format,
                                  static_cast<GLsizei>(new_params.SizeInBytes()), nullptr);
    } else {
        glTextureSubImage2D(new_surface->Texture().handle, 0, 0, 0,
                            static_cast<GLsizei>(dest_rect.GetWidth()),
                            static_cast<GLsizei>(dest_rect.GetHeight()), dest_format.format,
                            dest_format.type, nullptr);
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    pbo.Release();

    return new_surface;
}

Surface RasterizerCacheOpenGL::TryFindFramebufferSurface(VAddr cpu_addr) const {
    // Tries to find the GPU address of a framebuffer based on the CPU address. This is because
    // final output framebuffers are specified by CPU address, but internally our GPU cache uses
    // GPU addresses. We iterate through all cached framebuffers, and compare their starting CPU
    // address to the one provided. This is obviously not great, and won't work if the
    // framebuffer overlaps surfaces.

    std::vector<Surface> surfaces;
    for (const auto& surface : GetCache()) {
        const auto& params = surface.second->GetSurfaceParams();
        const VAddr surface_cpu_addr = params.GetCpuAddr();
        if (cpu_addr >= surface_cpu_addr && cpu_addr < (surface_cpu_addr + params.size_in_bytes)) {
            ASSERT_MSG(cpu_addr == surface_cpu_addr, "overlapping surfaces are unsupported");
            surfaces.push_back(surface.second);
        }
    }

    if (surfaces.empty()) {
        return {};
    }

    ASSERT_MSG(surfaces.size() == 1, ">1 surface is unsupported");

    return surfaces[0];
}

void RasterizerCacheOpenGL::ReserveSurface(const Surface& surface) {
    const auto& surface_reserve_key{SurfaceReserveKey::Create(surface->GetSurfaceParams())};
    surface_reserve[surface_reserve_key] = surface;
}

Surface RasterizerCacheOpenGL::TryGetReservedSurface(const SurfaceParams& params) {
    const auto& surface_reserve_key{SurfaceReserveKey::Create(params)};
    auto search{surface_reserve.find(surface_reserve_key)};
    if (search != surface_reserve.end()) {
        Register(search->second);
        return search->second;
    }
    return {};
}

} // namespace OpenGL
