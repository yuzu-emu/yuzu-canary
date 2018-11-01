// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "common/alignment.h"
#include "common/common_types.h"
#include "common/hash.h"
#include "common/math_util.h"
#include "video_core/engines/fermi_2d.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/rasterizer_cache.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_shader_gen.h"
#include "video_core/surface.h"
#include "video_core/textures/decoders.h"
#include "video_core/textures/texture.h"

namespace OpenGL {

class CachedSurface;
using Surface = std::shared_ptr<CachedSurface>;
using SurfaceSurfaceRect_Tuple = std::tuple<Surface, Surface, MathUtil::Rectangle<u32>>;

using SurfaceTarget = VideoCore::Surface::SurfaceTarget;
using SurfaceType = VideoCore::Surface::SurfaceType;
using PixelFormat = VideoCore::Surface::PixelFormat;
using ComponentType = VideoCore::Surface::ComponentType;

struct SurfaceParams {
    static std::string SurfaceTargetName(SurfaceTarget target) {
        switch (target) {
        case SurfaceTarget::Texture1D:
            return "Texture1D";
        case SurfaceTarget::Texture2D:
            return "Texture2D";
        case SurfaceTarget::Texture3D:
            return "Texture3D";
        case SurfaceTarget::Texture1DArray:
            return "Texture1DArray";
        case SurfaceTarget::Texture2DArray:
            return "Texture2DArray";
        case SurfaceTarget::TextureCubemap:
            return "TextureCubemap";
        case SurfaceTarget::TextureCubeArray:
            return "TextureCubeArray";
        default:
            LOG_CRITICAL(HW_GPU, "Unimplemented surface_target={}", static_cast<u32>(target));
            UNREACHABLE();
            return fmt::format("TextureUnknown({})", static_cast<u32>(target));
        }
    }

    u32 GetFormatBpp() const {
        return VideoCore::Surface::GetFormatBpp(pixel_format);
    }

    /// Returns the rectangle corresponding to this surface
    MathUtil::Rectangle<u32> GetRect(u32 mip_level = 0) const;

    /// Returns the total size of this surface in bytes, adjusted for compression
    std::size_t SizeInBytesRaw(bool ignore_tiled = false) const {
        const u32 compression_factor{GetCompressionFactor(pixel_format)};
        const u32 bytes_per_pixel{GetBytesPerPixel(pixel_format)};
        const size_t uncompressed_size{
            Tegra::Texture::CalculateSize((ignore_tiled ? false : is_tiled), bytes_per_pixel, width,
                                          height, depth, block_height, block_depth)};

        // Divide by compression_factor^2, as height and width are factored by this
        return uncompressed_size / (compression_factor * compression_factor);
    }

    /// Returns the size of this surface as an OpenGL texture in bytes
    std::size_t SizeInBytesGL() const {
        return SizeInBytesRaw(true);
    }

    /// Returns the size of this surface as a cube face in bytes
    std::size_t SizeInBytesCubeFace() const {
        return size_in_bytes / 6;
    }

    /// Returns the size of this surface as an OpenGL cube face in bytes
    std::size_t SizeInBytesCubeFaceGL() const {
        return size_in_bytes_gl / 6;
    }

    /// Returns the exact size of memory occupied by the texture in VRAM, including mipmaps.
    std::size_t MemorySize() const {
        std::size_t size = InnerMemorySize(false, is_layered);
        if (is_layered)
            return size * depth;
        return size;
    }

    /// Returns the exact size of the memory occupied by a layer in a texture in VRAM, including
    /// mipmaps.
    std::size_t LayerMemorySize() const {
        return InnerMemorySize(false, true);
    }

    /// Returns the size of a layer of this surface in OpenGL.
    std::size_t LayerSizeGL(u32 mip_level) const {
        return InnerMipmapMemorySize(mip_level, true, is_layered, false);
    }

    std::size_t GetMipmapSizeGL(u32 mip_level, bool ignore_compressed = true) const {
        std::size_t size = InnerMipmapMemorySize(mip_level, true, is_layered, ignore_compressed);
        if (is_layered)
            return size * depth;
        return size;
    }

    std::size_t GetMipmapLevelOffset(u32 mip_level) const {
        std::size_t offset = 0;
        for (u32 i = 0; i < mip_level; i++)
            offset += InnerMipmapMemorySize(i, false, is_layered);
        return offset;
    }

    std::size_t GetMipmapLevelOffsetGL(u32 mip_level) const {
        std::size_t offset = 0;
        for (u32 i = 0; i < mip_level; i++)
            offset += InnerMipmapMemorySize(i, true, is_layered);
        return offset;
    }

    u32 MipWidth(u32 mip_level) const {
        return std::max(1U, width >> mip_level);
    }

    u32 MipHeight(u32 mip_level) const {
        return std::max(1U, height >> mip_level);
    }

    u32 MipDepth(u32 mip_level) const {
        return std::max(1U, depth >> mip_level);
    }

    // Auto block resizing algorithm from:
    // https://cgit.freedesktop.org/mesa/mesa/tree/src/gallium/drivers/nouveau/nv50/nv50_miptree.c
    u32 MipBlockHeight(u32 mip_level) const {
        if (mip_level == 0)
            return block_height;
        u32 alt_height = MipHeight(mip_level);
        u32 h = GetDefaultBlockHeight(pixel_format);
        u32 blocks_in_y = (alt_height + h - 1) / h;
        u32 bh = 16;
        while (bh > 1 && blocks_in_y <= bh * 4) {
            bh >>= 1;
        }
        return bh;
    }

    u32 MipBlockDepth(u32 mip_level) const {
        if (mip_level == 0)
            return block_depth;
        if (is_layered)
            return 1;
        u32 depth = MipDepth(mip_level);
        u32 bd = 32;
        while (bd > 1 && depth * 2 <= bd) {
            bd >>= 1;
        }
        if (bd == 32) {
            u32 bh = MipBlockHeight(mip_level);
            if (bh >= 4)
                return 16;
        }
        return bd;
    }

    /// Creates SurfaceParams from a texture configuration
    static SurfaceParams CreateForTexture(const Tegra::Texture::FullTextureInfo& config,
                                          const GLShader::SamplerEntry& entry);

    /// Creates SurfaceParams from a framebuffer configuration
    static SurfaceParams CreateForFramebuffer(std::size_t index);

    /// Creates SurfaceParams for a depth buffer configuration
    static SurfaceParams CreateForDepthBuffer(
        u32 zeta_width, u32 zeta_height, Tegra::GPUVAddr zeta_address, Tegra::DepthFormat format,
        u32 block_width, u32 block_height, u32 block_depth,
        Tegra::Engines::Maxwell3D::Regs::InvMemoryLayout type);

    /// Creates SurfaceParams for a Fermi2D surface copy
    static SurfaceParams CreateForFermiCopySurface(
        const Tegra::Engines::Fermi2D::Regs::Surface& config);

    /// Checks if surfaces are compatible for caching
    bool IsCompatibleSurface(const SurfaceParams& other) const {
        return std::tie(pixel_format, type, width, height, target, depth) ==
               std::tie(other.pixel_format, other.type, other.width, other.height, other.target,
                        other.depth);
    }

    /// Initializes parameters for caching, should be called after everything has been initialized
    void InitCacheParameters(Tegra::GPUVAddr gpu_addr);

    bool is_tiled;
    u32 block_width;
    u32 block_height;
    u32 block_depth;
    PixelFormat pixel_format;
    ComponentType component_type;
    SurfaceType type;
    u32 width;
    u32 height;
    u32 depth;
    u32 unaligned_height;
    SurfaceTarget target;
    u32 max_mip_level;
    bool is_layered;
    bool srgb_conversion;
    // Parameters used for caching
    VAddr addr;
    Tegra::GPUVAddr gpu_addr;
    std::size_t size_in_bytes;
    std::size_t size_in_bytes_gl;

    // Render target specific parameters, not used in caching
    struct {
        u32 index;
        u32 array_mode;
        u32 volume;
        u32 layer_stride;
        u32 base_layer;
    } rt;

private:
    std::size_t InnerMipmapMemorySize(u32 mip_level, bool force_gl = false, bool layer_only = false,
                                      bool uncompressed = false) const;
    std::size_t InnerMemorySize(bool force_gl = false, bool layer_only = false,
                                bool uncompressed = false) const;
};

}; // namespace OpenGL

/// Hashable variation of SurfaceParams, used for a key in the surface cache
struct SurfaceReserveKey : Common::HashableStruct<OpenGL::SurfaceParams> {
    static SurfaceReserveKey Create(const OpenGL::SurfaceParams& params) {
        SurfaceReserveKey res;
        res.state = params;
        res.state.gpu_addr = {}; // Ignore GPU vaddr in caching
        res.state.rt = {};       // Ignore rt config in caching
        return res;
    }
};
namespace std {
template <>
struct hash<SurfaceReserveKey> {
    std::size_t operator()(const SurfaceReserveKey& k) const {
        return k.Hash();
    }
};
} // namespace std

namespace OpenGL {

class CachedSurface final : public RasterizerCacheObject {
public:
    CachedSurface(const SurfaceParams& params);

    VAddr GetAddr() const override {
        return params.addr;
    }

    std::size_t GetSizeInBytes() const override {
        return cached_size_in_bytes;
    }

    void Flush() override {
        FlushGLBuffer();
    }

    const OGLTexture& Texture() const {
        return texture;
    }

    GLenum Target() const {
        return gl_target;
    }

    const SurfaceParams& GetSurfaceParams() const {
        return params;
    }

    // Read/Write data in Switch memory to/from gl_buffer
    void LoadGLBuffer();
    void FlushGLBuffer();

    // Upload data in gl_buffer to this surface's texture
    void UploadGLTexture(GLuint read_fb_handle, GLuint draw_fb_handle);

private:
    void UploadGLMipmapTexture(u32 mip_map, GLuint read_fb_handle, GLuint draw_fb_handle);

    OGLTexture texture;
    std::vector<std::vector<u8>> gl_buffer;
    SurfaceParams params;
    GLenum gl_target;
    std::size_t cached_size_in_bytes;
};

class RasterizerCacheOpenGL final : public RasterizerCache<Surface> {
public:
    RasterizerCacheOpenGL();

    /// Get a surface based on the texture configuration
    Surface GetTextureSurface(const Tegra::Texture::FullTextureInfo& config,
                              const GLShader::SamplerEntry& entry);

    /// Get the depth surface based on the framebuffer configuration
    Surface GetDepthBufferSurface(bool preserve_contents);

    /// Get the color surface based on the framebuffer configuration and the specified render target
    Surface GetColorBufferSurface(std::size_t index, bool preserve_contents);

    /// Tries to find a framebuffer using on the provided CPU address
    Surface TryFindFramebufferSurface(VAddr addr) const;

    /// Copies the contents of one surface to another
    void FermiCopySurface(const Tegra::Engines::Fermi2D::Regs::Surface& src_config,
                          const Tegra::Engines::Fermi2D::Regs::Surface& dst_config);

private:
    void LoadSurface(const Surface& surface);
    Surface GetSurface(const SurfaceParams& params, bool preserve_contents = true);

    /// Gets an uncached surface, creating it if need be
    Surface GetUncachedSurface(const SurfaceParams& params);

    /// Recreates a surface with new parameters
    Surface RecreateSurface(const Surface& old_surface, const SurfaceParams& new_params);

    /// Reserves a unique surface that can be reused later
    void ReserveSurface(const Surface& surface);

    /// Tries to get a reserved surface for the specified parameters
    Surface TryGetReservedSurface(const SurfaceParams& params);

    /// Performs a slow but accurate surface copy, flushing to RAM and reinterpreting the data
    void AccurateCopySurface(const Surface& src_surface, const Surface& dst_surface);

    /// The surface reserve is a "backup" cache, this is where we put unique surfaces that have
    /// previously been used. This is to prevent surfaces from being constantly created and
    /// destroyed when used with different surface parameters.
    std::unordered_map<SurfaceReserveKey, Surface> surface_reserve;

    OGLFramebuffer read_framebuffer;
    OGLFramebuffer draw_framebuffer;

    /// Use a Pixel Buffer Object to download the previous texture and then upload it to the new one
    /// using the new format.
    OGLBuffer copy_pbo;
};

} // namespace OpenGL
