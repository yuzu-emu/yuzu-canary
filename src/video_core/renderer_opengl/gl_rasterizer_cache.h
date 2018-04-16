// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <memory>
#include <set>
#include <tuple>
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-local-typedefs"
#endif
#include <boost/icl/interval_map.hpp>
#include <boost/icl/interval_set.hpp>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#include <glad/glad.h>
#include "common/assert.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/math_util.h"
#include "video_core/gpu.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/textures/texture.h"

struct CachedSurface;
using Surface = std::shared_ptr<CachedSurface>;
using SurfaceSet = std::set<Surface>;

using SurfaceRegions = boost::icl::interval_set<VAddr>;
using SurfaceMap = boost::icl::interval_map<VAddr, Surface>;
using SurfaceCache = boost::icl::interval_map<VAddr, SurfaceSet>;

using SurfaceInterval = SurfaceCache::interval_type;
static_assert(std::is_same<SurfaceRegions::interval_type, SurfaceCache::interval_type>() &&
                  std::is_same<SurfaceMap::interval_type, SurfaceCache::interval_type>(),
              "incorrect interval types");

using SurfaceRect_Tuple = std::tuple<Surface, MathUtil::Rectangle<u32>>;
using SurfaceSurfaceRect_Tuple = std::tuple<Surface, Surface, MathUtil::Rectangle<u32>>;

using PageMap = boost::icl::interval_map<u64, int>;

enum class ScaleMatch {
    Exact,   // only accept same res scale
    Upscale, // only allow higher scale than params
    Ignore   // accept every scaled res
};

struct SurfaceParams {
    enum class PixelFormat {
        // Texture and color buffer formats
        RGBA8 = 0,
        RGB5A1 = 1,
        RGB565 = 2,
        RG11FB10F = 3,

        // Compressed Texture formats
        BC1 = 4,
        BC2 = 5,
        BC3 = 6,

        Invalid = 255,
    };

    enum class SurfaceType {
        Color = 0,
        Texture = 1,
        Depth = 2,
        DepthStencil = 3,
        Fill = 4,
        Invalid = 5
    };

    static constexpr unsigned int GetFormatBpp(PixelFormat format) {
        if (format == PixelFormat::Invalid)
            return 0;

        constexpr std::array<unsigned int, 7> bpp_table = {
            32,  // RGBA8
            16,  // RGB5A1
            16,  // RGB565
            32,  // RG11FB10F
            64,  // BC1
            128, // BC2
            128, // BC3
        };

        ASSERT(static_cast<size_t>(format) < bpp_table.size());
        return bpp_table[static_cast<size_t>(format)];
    }
    unsigned int GetFormatBpp() const {
        return GetFormatBpp(pixel_format);
    }

    static PixelFormat PixelFormatFromRenderTargetFormat(Tegra::RenderTargetFormat format) {
        switch (format) {
        case Tegra::RenderTargetFormat::RGBA8_UNORM:
            return PixelFormat::RGBA8;
        default:
            LOG_CRITICAL(HW_GPU, "Unimplemented format=%d", format);
            UNREACHABLE();
        }
    }

    static PixelFormat PixelFormatFromGPUPixelFormat(Tegra::FramebufferConfig::PixelFormat format) {
        switch (format) {
        case Tegra::FramebufferConfig::PixelFormat::ABGR8:
            return PixelFormat::RGBA8;
        default:
            LOG_CRITICAL(HW_GPU, "Unimplemented format=%d", format);
            UNREACHABLE();
        }
    }

    static PixelFormat PixelFormatFromTextureFormat(Tegra::Texture::TextureFormat format) {
        // TODO(Subv): Properly implement this
        switch (format) {
        case Tegra::Texture::TextureFormat::A8R8G8B8:
            return PixelFormat::RGBA8;
        case Tegra::Texture::TextureFormat::BC1:
            return PixelFormat::BC1;
        case Tegra::Texture::TextureFormat::BC2:
            return PixelFormat::BC2;
        case Tegra::Texture::TextureFormat::BC3:
            return PixelFormat::BC3;
        default:
            LOG_CRITICAL(HW_GPU, "Unimplemented format=%d", format);
            UNREACHABLE();
        }
    }

    static bool CheckFormatsBlittable(PixelFormat pixel_format_a, PixelFormat pixel_format_b) {
        SurfaceType a_type = GetFormatType(pixel_format_a);
        SurfaceType b_type = GetFormatType(pixel_format_b);

        if ((a_type == SurfaceType::Color || a_type == SurfaceType::Texture) &&
            (b_type == SurfaceType::Color || b_type == SurfaceType::Texture)) {
            return true;
        }

        if (a_type == SurfaceType::Depth && b_type == SurfaceType::Depth) {
            return true;
        }

        if (a_type == SurfaceType::DepthStencil && b_type == SurfaceType::DepthStencil) {
            return true;
        }

        return false;
    }

    static SurfaceType GetFormatType(PixelFormat pixel_format) {
        if ((unsigned int)pixel_format <= static_cast<unsigned int>(PixelFormat::RGBA8)) {
            return SurfaceType::Color;
        }

        if ((unsigned int)pixel_format <= static_cast<unsigned int>(PixelFormat::BC3)) {
            return SurfaceType::Texture;
        }

        // TODO(Subv): Implement the other formats
        ASSERT(false);

        return SurfaceType::Invalid;
    }

    /// Update the params "size", "end" and "type" from the already set "addr", "width", "height"
    /// and "pixel_format"
    void UpdateParams() {
        if (stride == 0) {
            stride = width;
        }
        type = GetFormatType(pixel_format);
        size = !is_tiled ? BytesInPixels(stride * (height - 1) + width)
                         : BytesInPixels(stride * 8 * (height / 8 - 1) + width * 8);
        end = addr + size;
    }

    SurfaceInterval GetInterval() const {
        return SurfaceInterval::right_open(addr, end);
    }

    // Returns the outer rectangle containing "interval"
    SurfaceParams FromInterval(SurfaceInterval interval) const;

    SurfaceInterval GetSubRectInterval(MathUtil::Rectangle<u32> unscaled_rect) const;

    // Returns the region of the biggest valid rectange within interval
    SurfaceInterval GetCopyableInterval(const Surface& src_surface) const;

    u32 GetScaledWidth() const {
        return width * res_scale;
    }

    u32 GetScaledHeight() const {
        return height * res_scale;
    }

    MathUtil::Rectangle<u32> GetRect() const {
        return {0, height, width, 0};
    }

    MathUtil::Rectangle<u32> GetScaledRect() const {
        return {0, GetScaledHeight(), GetScaledWidth(), 0};
    }

    u64 PixelsInBytes(u64 size) const {
        return size * CHAR_BIT / GetFormatBpp(pixel_format);
    }

    u64 BytesInPixels(u64 pixels) const {
        return pixels * GetFormatBpp(pixel_format) / CHAR_BIT;
    }

    bool ExactMatch(const SurfaceParams& other_surface) const;
    bool CanSubRect(const SurfaceParams& sub_surface) const;
    bool CanExpand(const SurfaceParams& expanded_surface) const;
    bool CanTexCopy(const SurfaceParams& texcopy_params) const;

    MathUtil::Rectangle<u32> GetSubRect(const SurfaceParams& sub_surface) const;
    MathUtil::Rectangle<u32> GetScaledSubRect(const SurfaceParams& sub_surface) const;

    VAddr addr = 0;
    VAddr end = 0;
    u64 size = 0;

    u32 width = 0;
    u32 height = 0;
    u32 stride = 0;
    u32 block_height = 0;
    u16 res_scale = 1;

    bool is_tiled = true;
    PixelFormat pixel_format = PixelFormat::Invalid;
    SurfaceType type = SurfaceType::Invalid;
};

struct CachedSurface : SurfaceParams {
    bool CanFill(const SurfaceParams& dest_surface, SurfaceInterval fill_interval) const;
    bool CanCopy(const SurfaceParams& dest_surface, SurfaceInterval copy_interval) const;

    bool IsRegionValid(SurfaceInterval interval) const {
        return (invalid_regions.find(interval) == invalid_regions.end());
    }

    bool IsSurfaceFullyInvalid() const {
        return (invalid_regions & GetInterval()) == SurfaceRegions(GetInterval());
    }

    bool registered = false;
    SurfaceRegions invalid_regions;

    u64 fill_size = 0; /// Number of bytes to read from fill_data
    std::array<u8, 4> fill_data;

    OGLTexture texture;

    static constexpr unsigned int GetGLBytesPerPixel(PixelFormat format) {
        if (format == PixelFormat::Invalid)
            return 0;

        return SurfaceParams::GetFormatBpp(format) / 8;
    }

    std::unique_ptr<u8[]> gl_buffer;
    size_t gl_buffer_size = 0;

    // Read/Write data in Switch memory to/from gl_buffer
    void LoadGLBuffer(VAddr load_start, VAddr load_end);
    void FlushGLBuffer(VAddr flush_start, VAddr flush_end);

    // Upload/Download data in gl_buffer in/to this surface's texture
    void UploadGLTexture(const MathUtil::Rectangle<u32>& rect, GLuint read_fb_handle,
                         GLuint draw_fb_handle);
    void DownloadGLTexture(const MathUtil::Rectangle<u32>& rect, GLuint read_fb_handle,
                           GLuint draw_fb_handle);
};

class RasterizerCacheOpenGL : NonCopyable {
public:
    RasterizerCacheOpenGL();
    ~RasterizerCacheOpenGL();

    /// Blit one surface's texture to another
    bool BlitSurfaces(const Surface& src_surface, const MathUtil::Rectangle<u32>& src_rect,
                      const Surface& dst_surface, const MathUtil::Rectangle<u32>& dst_rect);

    void ConvertD24S8toABGR(GLuint src_tex, const MathUtil::Rectangle<u32>& src_rect,
                            GLuint dst_tex, const MathUtil::Rectangle<u32>& dst_rect);

    /// Copy one surface's region to another
    void CopySurface(const Surface& src_surface, const Surface& dst_surface,
                     SurfaceInterval copy_interval);

    /// Load a texture from Switch memory to OpenGL and cache it (if not already cached)
    Surface GetSurface(const SurfaceParams& params, ScaleMatch match_res_scale,
                       bool load_if_create);

    /// Attempt to find a subrect (resolution scaled) of a surface, otherwise loads a texture from
    /// Switch memory to OpenGL and caches it (if not already cached)
    SurfaceRect_Tuple GetSurfaceSubRect(const SurfaceParams& params, ScaleMatch match_res_scale,
                                        bool load_if_create);

    /// Get a surface based on the texture configuration
    Surface GetTextureSurface(const Tegra::Texture::FullTextureInfo& config);

    /// Get the color and depth surfaces based on the framebuffer configuration
    SurfaceSurfaceRect_Tuple GetFramebufferSurfaces(bool using_color_fb, bool using_depth_fb,
                                                    const MathUtil::Rectangle<s32>& viewport);

    /// Get a surface that matches the fill config
    Surface GetFillSurface(const void* config);

    /// Get a surface that matches a "texture copy" display transfer config
    SurfaceRect_Tuple GetTexCopySurface(const SurfaceParams& params);

    /// Write any cached resources overlapping the region back to memory (if dirty)
    void FlushRegion(VAddr addr, u64 size, Surface flush_surface = nullptr);

    /// Mark region as being invalidated by region_owner (nullptr if Switch memory)
    void InvalidateRegion(VAddr addr, u64 size, const Surface& region_owner);

    /// Flush all cached resources tracked by this cache manager
    void FlushAll();

private:
    void DuplicateSurface(const Surface& src_surface, const Surface& dest_surface);

    /// Update surface's texture for given region when necessary
    void ValidateSurface(const Surface& surface, VAddr addr, u64 size);

    /// Create a new surface
    Surface CreateSurface(const SurfaceParams& params);

    /// Register surface into the cache
    void RegisterSurface(const Surface& surface);

    /// Remove surface from the cache
    void UnregisterSurface(const Surface& surface);

    /// Increase/decrease the number of surface in pages touching the specified region
    void UpdatePagesCachedCount(VAddr addr, u64 size, int delta);

    SurfaceCache surface_cache;
    PageMap cached_pages;
    SurfaceMap dirty_regions;
    SurfaceSet remove_surfaces;

    OGLFramebuffer read_framebuffer;
    OGLFramebuffer draw_framebuffer;

    OGLVertexArray attributeless_vao;
    OGLBuffer d24s8_abgr_buffer;
    GLsizeiptr d24s8_abgr_buffer_size;
    OGLProgram d24s8_abgr_shader;
    GLint d24s8_abgr_tbo_size_u_id;
    GLint d24s8_abgr_viewport_u_id;
};
