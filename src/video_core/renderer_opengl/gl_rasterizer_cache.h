// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
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
using SurfaceSurfaceRect_Tuple = std::tuple<Surface, Surface, Common::Rectangle<u32>>;

using SurfaceTarget = VideoCore::Surface::SurfaceTarget;
using SurfaceType = VideoCore::Surface::SurfaceType;
using PixelFormat = VideoCore::Surface::PixelFormat;
using ComponentType = VideoCore::Surface::ComponentType;
using Maxwell = Tegra::Engines::Maxwell3D::Regs;

struct SurfaceParams {
    enum class SurfaceClass {
        Uploaded,
        RenderTarget,
        DepthBuffer,
        Copy,
    };

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
    Common::Rectangle<u32> GetRect(u32 mip_level = 0) const;

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

    std::size_t GetMipmapSingleSize(u32 mip_level) const {
        return InnerMipmapMemorySize(mip_level, false, is_layered);
    }

    u32 MipWidth(u32 mip_level) const {
        return std::max(1U, width >> mip_level);
    }

    u32 MipWidthGobAligned(u32 mip_level) const {
        return Common::AlignUp(std::max(1U, width >> mip_level), 64U * 8U / GetFormatBpp());
    }

    u32 MipHeight(u32 mip_level) const {
        return std::max(1U, height >> mip_level);
    }

    u32 MipDepth(u32 mip_level) const {
        return is_layered ? depth : std::max(1U, depth >> mip_level);
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
        if (mip_level == 0) {
            return block_depth;
        }

        if (is_layered) {
            return 1;
        }

        const u32 mip_depth = MipDepth(mip_level);
        u32 bd = 32;
        while (bd > 1 && mip_depth * 2 <= bd) {
            bd >>= 1;
        }

        if (bd == 32) {
            const u32 bh = MipBlockHeight(mip_level);
            if (bh >= 4) {
                return 16;
            }
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
        if (std::tie(pixel_format, type, width, height, target, depth, is_tiled) ==
            std::tie(other.pixel_format, other.type, other.width, other.height, other.target,
                     other.depth, other.is_tiled)) {
            if (!is_tiled)
                return true;
            return std::tie(block_height, block_depth, tile_width_spacing) ==
                   std::tie(other.block_height, other.block_depth, other.tile_width_spacing);
        }
        return false;
    }

    /// Initializes parameters for caching, should be called after everything has been initialized
    void InitCacheParameters(Tegra::GPUVAddr gpu_addr);

    std::string TargetName() const {
        switch (target) {
        case SurfaceTarget::Texture1D:
            return "1D";
        case SurfaceTarget::Texture2D:
            return "2D";
        case SurfaceTarget::Texture3D:
            return "3D";
        case SurfaceTarget::Texture1DArray:
            return "1DArray";
        case SurfaceTarget::Texture2DArray:
            return "2DArray";
        case SurfaceTarget::TextureCubemap:
            return "Cube";
        default:
            LOG_CRITICAL(HW_GPU, "Unimplemented surface_target={}", static_cast<u32>(target));
            UNREACHABLE();
            return fmt::format("TUK({})", static_cast<u32>(target));
        }
    }

    std::string ClassName() const {
        switch (identity) {
        case SurfaceClass::Uploaded:
            return "UP";
        case SurfaceClass::RenderTarget:
            return "RT";
        case SurfaceClass::DepthBuffer:
            return "DB";
        case SurfaceClass::Copy:
            return "CP";
        default:
            LOG_CRITICAL(HW_GPU, "Unimplemented surface_class={}", static_cast<u32>(identity));
            UNREACHABLE();
            return fmt::format("CUK({})", static_cast<u32>(identity));
        }
    }

    std::string IdentityString() const {
        return ClassName() + '_' + TargetName() + '_' + (is_tiled ? 'T' : 'L');
    }

    bool is_tiled;
    u32 block_width;
    u32 block_height;
    u32 block_depth;
    u32 tile_width_spacing;
    PixelFormat pixel_format;
    ComponentType component_type;
    SurfaceType type;
    u32 width;
    u32 height;
    u32 depth;
    u32 unaligned_height;
    u32 pitch;
    SurfaceTarget target;
    SurfaceClass identity;
    u32 max_mip_level;
    bool is_layered;
    bool is_array;
    bool srgb_conversion;
    // Parameters used for caching
    u8* host_ptr;
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
        res.state.identity = {}; // Ignore the origin of the texture
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

class RasterizerOpenGL;

class CachedSurface final : public RasterizerCacheObject {
public:
    explicit CachedSurface(const SurfaceParams& params);

    VAddr GetCpuAddr() const override {
        return cpu_addr;
    }

    std::size_t GetSizeInBytes() const override {
        return cached_size_in_bytes;
    }

    std::size_t GetMemorySize() const {
        return memory_size;
    }

    void Flush() override {
        FlushGLBuffer();
    }

    const OGLTexture& Texture() const {
        return texture;
    }

    const OGLTexture& Texture(bool as_array) {
        if (params.is_array == as_array) {
            return texture;
        } else {
            EnsureTextureDiscrepantView();
            return discrepant_view;
        }
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

    void UpdateSwizzle(Tegra::Texture::SwizzleSource swizzle_x,
                       Tegra::Texture::SwizzleSource swizzle_y,
                       Tegra::Texture::SwizzleSource swizzle_z,
                       Tegra::Texture::SwizzleSource swizzle_w);

    void MarkReinterpreted() {
        reinterpreted = true;
    }

    bool IsReinterpreted() const {
        return reinterpreted;
    }

    void MarkForReload(bool reload) {
        must_reload = reload;
    }

    bool MustReload() const {
        return must_reload;
    }

    bool IsUploaded() const {
        return params.identity == SurfaceParams::SurfaceClass::Uploaded;
    }

private:
    void UploadGLMipmapTexture(u32 mip_map, GLuint read_fb_handle, GLuint draw_fb_handle);

    void EnsureTextureDiscrepantView();

    OGLTexture texture;
    OGLTexture discrepant_view;
    std::vector<std::vector<u8>> gl_buffer;
    SurfaceParams params{};
    GLenum gl_target{};
    GLenum gl_internal_format{};
    std::size_t cached_size_in_bytes{};
    std::array<GLenum, 4> swizzle{GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA};
    std::size_t memory_size;
    bool reinterpreted = false;
    bool must_reload = false;
    VAddr cpu_addr{};
};

class RasterizerCacheOpenGL final : public RasterizerCache<Surface> {
public:
    explicit RasterizerCacheOpenGL(RasterizerOpenGL& rasterizer);

    /// Get a surface based on the texture configuration
    Surface GetTextureSurface(const Tegra::Texture::FullTextureInfo& config,
                              const GLShader::SamplerEntry& entry);

    /// Get the depth surface based on the framebuffer configuration
    Surface GetDepthBufferSurface(bool preserve_contents);

    /// Get the color surface based on the framebuffer configuration and the specified render target
    Surface GetColorBufferSurface(std::size_t index, bool preserve_contents);

    /// Tries to find a framebuffer using on the provided CPU address
    Surface TryFindFramebufferSurface(const u8* host_ptr) const;

    /// Copies the contents of one surface to another
    void FermiCopySurface(const Tegra::Engines::Fermi2D::Regs::Surface& src_config,
                          const Tegra::Engines::Fermi2D::Regs::Surface& dst_config,
                          const Common::Rectangle<u32>& src_rect,
                          const Common::Rectangle<u32>& dst_rect);

    void SignalPreDrawCall();
    void SignalPostDrawCall();

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

    // Partialy reinterpret a surface based on a triggering_surface that collides with it.
    // returns true if the reinterpret was successful, false in case it was not.
    bool PartialReinterpretSurface(Surface triggering_surface, Surface intersect);

    /// Performs a slow but accurate surface copy, flushing to RAM and reinterpreting the data
    void AccurateCopySurface(const Surface& src_surface, const Surface& dst_surface);
    void FastLayeredCopySurface(const Surface& src_surface, const Surface& dst_surface);
    void FastCopySurface(const Surface& src_surface, const Surface& dst_surface);
    void CopySurface(const Surface& src_surface, const Surface& dst_surface,
                     const GLuint copy_pbo_handle, const GLenum src_attachment = 0,
                     const GLenum dst_attachment = 0, const std::size_t cubemap_face = 0);

    /// The surface reserve is a "backup" cache, this is where we put unique surfaces that have
    /// previously been used. This is to prevent surfaces from being constantly created and
    /// destroyed when used with different surface parameters.
    std::unordered_map<SurfaceReserveKey, Surface> surface_reserve;

    OGLFramebuffer read_framebuffer;
    OGLFramebuffer draw_framebuffer;

    bool texception = false;

    /// Use a Pixel Buffer Object to download the previous texture and then upload it to the new one
    /// using the new format.
    OGLBuffer copy_pbo;

    std::array<Surface, Maxwell::NumRenderTargets> last_color_buffers;
    std::array<Surface, Maxwell::NumRenderTargets> current_color_buffers;
    Surface last_depth_buffer;

    using SurfaceIntervalCache = boost::icl::interval_map<CacheAddr, Surface>;
    using SurfaceInterval = typename SurfaceIntervalCache::interval_type;

    static auto GetReinterpretInterval(const Surface& object) {
        return SurfaceInterval::right_open(object->GetCacheAddr() + 1,
                                           object->GetCacheAddr() + object->GetMemorySize() - 1);
    }

    // Reinterpreted surfaces are very fragil as the game may keep rendering into them.
    SurfaceIntervalCache reinterpreted_surfaces;

    void RegisterReinterpretSurface(Surface reinterpret_surface) {
        auto interval = GetReinterpretInterval(reinterpret_surface);
        reinterpreted_surfaces.insert({interval, reinterpret_surface});
        reinterpret_surface->MarkReinterpreted();
    }

    Surface CollideOnReinterpretedSurface(CacheAddr addr) const {
        const SurfaceInterval interval{addr};
        for (auto& pair :
             boost::make_iterator_range(reinterpreted_surfaces.equal_range(interval))) {
            return pair.second;
        }
        return nullptr;
    }

    /// Unregisters an object from the cache
    void UnregisterSurface(const Surface& object) {
        if (object->IsReinterpreted()) {
            auto interval = GetReinterpretInterval(object);
            reinterpreted_surfaces.erase(interval);
        }
        Unregister(object);
    }
};

} // namespace OpenGL
