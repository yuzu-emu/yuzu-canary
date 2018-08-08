// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <map>
#include <memory>
#include <vector>
#include <boost/icl/interval_map.hpp>

#include "common/common_types.h"
#include "common/math_util.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/textures/texture.h"

class CachedSurface;
using Surface = std::shared_ptr<CachedSurface>;
using SurfaceSurfaceRect_Tuple = std::tuple<Surface, Surface, MathUtil::Rectangle<u32>>;
using PageMap = boost::icl::interval_map<u64, int>;

struct SurfaceParams {
    enum class PixelFormat {
        ABGR8 = 0,
        B5G6R5 = 1,
        A2B10G10R10 = 2,
        A1B5G5R5 = 3,
        R8 = 4,
        RGBA16F = 5,
        R11FG11FB10F = 6,
        RGBA32UI = 7,
        DXT1 = 8,
        DXT23 = 9,
        DXT45 = 10,
        DXN1 = 11, // This is also known as BC4
        BC7U = 12,
        ASTC_2D_4X4 = 13,
        G8R8 = 14,
        BGRA8 = 15,
        RGBA32F = 16,
        RG32F = 17,
        R32F = 18,
        R16F = 19,
        R16UNORM = 20,
        RG16 = 21,
        RG16F = 22,
        RG16UI = 23,
        RG16I = 24,
        RG16S = 25,
        RGB32F = 26,
        SRGBA8 = 27,

        MaxColorFormat,

        // DepthStencil formats
        Z24S8 = 28,
        S8Z24 = 29,
        Z32F = 30,
        Z16 = 31,
        Z32FS8 = 32,

        MaxDepthStencilFormat,

        Max = MaxDepthStencilFormat,
        Invalid = 255,
    };

    static constexpr size_t MaxPixelFormat = static_cast<size_t>(PixelFormat::Max);

    enum class ComponentType {
        Invalid = 0,
        SNorm = 1,
        UNorm = 2,
        SInt = 3,
        UInt = 4,
        Float = 5,
    };

    enum class SurfaceType {
        ColorTexture = 0,
        Depth = 1,
        DepthStencil = 2,
        Fill = 3,
        Invalid = 4,
    };

    /**
     * Gets the compression factor for the specified PixelFormat. This applies to just the
     * "compressed width" and "compressed height", not the overall compression factor of a
     * compressed image. This is used for maintaining proper surface sizes for compressed
     * texture formats.
     */
    static constexpr u32 GetCompressionFactor(PixelFormat format) {
        if (format == PixelFormat::Invalid)
            return 0;

        constexpr std::array<u32, MaxPixelFormat> compression_factor_table = {{
            1, // ABGR8
            1, // B5G6R5
            1, // A2B10G10R10
            1, // A1B5G5R5
            1, // R8
            1, // RGBA16F
            1, // R11FG11FB10F
            1, // RGBA32UI
            4, // DXT1
            4, // DXT23
            4, // DXT45
            4, // DXN1
            4, // BC7U
            4, // ASTC_2D_4X4
            1, // G8R8
            1, // BGRA8
            1, // RGBA32F
            1, // RG32F
            1, // R32F
            1, // R16F
            1, // R16UNORM
            1, // RG16
            1, // RG16F
            1, // RG16UI
            1, // RG16I
            1, // RG16S
            1, // RGB32F
            1, // SRGBA8
            1, // Z24S8
            1, // S8Z24
            1, // Z32F
            1, // Z16
            1, // Z32FS8
        }};

        ASSERT(static_cast<size_t>(format) < compression_factor_table.size());
        return compression_factor_table[static_cast<size_t>(format)];
    }

    static constexpr u32 GetFormatBpp(PixelFormat format) {
        if (format == PixelFormat::Invalid)
            return 0;

        constexpr std::array<u32, MaxPixelFormat> bpp_table = {{
            32,  // ABGR8
            16,  // B5G6R5
            32,  // A2B10G10R10
            16,  // A1B5G5R5
            8,   // R8
            64,  // RGBA16F
            32,  // R11FG11FB10F
            128, // RGBA32UI
            64,  // DXT1
            128, // DXT23
            128, // DXT45
            64,  // DXN1
            128, // BC7U
            32,  // ASTC_2D_4X4
            16,  // G8R8
            32,  // BGRA8
            128, // RGBA32F
            64,  // RG32F
            32,  // R32F
            16,  // R16F
            16,  // R16UNORM
            32,  // RG16
            32,  // RG16F
            32,  // RG16UI
            32,  // RG16I
            32,  // RG16S
            96,  // RGB32F
            32,  // SRGBA8
            32,  // Z24S8
            32,  // S8Z24
            32,  // Z32F
            16,  // Z16
            64,  // Z32FS8
        }};

        ASSERT(static_cast<size_t>(format) < bpp_table.size());
        return bpp_table[static_cast<size_t>(format)];
    }

    u32 GetFormatBpp() const {
        return GetFormatBpp(pixel_format);
    }

    static PixelFormat PixelFormatFromDepthFormat(Tegra::DepthFormat format) {
        switch (format) {
        case Tegra::DepthFormat::S8_Z24_UNORM:
            return PixelFormat::S8Z24;
        case Tegra::DepthFormat::Z24_S8_UNORM:
            return PixelFormat::Z24S8;
        case Tegra::DepthFormat::Z32_FLOAT:
            return PixelFormat::Z32F;
        case Tegra::DepthFormat::Z16_UNORM:
            return PixelFormat::Z16;
        case Tegra::DepthFormat::Z32_S8_X24_FLOAT:
            return PixelFormat::Z32FS8;
        default:
            LOG_CRITICAL(HW_GPU, "Unimplemented format={}", static_cast<u32>(format));
            UNREACHABLE();
        }
    }

    static PixelFormat PixelFormatFromRenderTargetFormat(Tegra::RenderTargetFormat format) {
        switch (format) {
        // TODO (Hexagon12): Converting SRGBA to RGBA is a hack and doesn't completely correct the
        // gamma.
        case Tegra::RenderTargetFormat::RGBA8_SRGB:
        case Tegra::RenderTargetFormat::RGBA8_UNORM:
            return PixelFormat::ABGR8;
        case Tegra::RenderTargetFormat::BGRA8_UNORM:
            return PixelFormat::BGRA8;
        case Tegra::RenderTargetFormat::RGB10_A2_UNORM:
            return PixelFormat::A2B10G10R10;
        case Tegra::RenderTargetFormat::RGBA16_FLOAT:
            return PixelFormat::RGBA16F;
        case Tegra::RenderTargetFormat::RGBA32_FLOAT:
            return PixelFormat::RGBA32F;
        case Tegra::RenderTargetFormat::RG32_FLOAT:
            return PixelFormat::RG32F;
        case Tegra::RenderTargetFormat::R11G11B10_FLOAT:
            return PixelFormat::R11FG11FB10F;
        case Tegra::RenderTargetFormat::B5G6R5_UNORM:
            return PixelFormat::B5G6R5;
        case Tegra::RenderTargetFormat::RGBA32_UINT:
            return PixelFormat::RGBA32UI;
        case Tegra::RenderTargetFormat::R8_UNORM:
            return PixelFormat::R8;
        case Tegra::RenderTargetFormat::RG16_FLOAT:
            return PixelFormat::RG16F;
        case Tegra::RenderTargetFormat::RG16_UINT:
            return PixelFormat::RG16UI;
        case Tegra::RenderTargetFormat::RG16_SINT:
            return PixelFormat::RG16I;
        case Tegra::RenderTargetFormat::RG16_UNORM:
            return PixelFormat::RG16;
        case Tegra::RenderTargetFormat::RG16_SNORM:
            return PixelFormat::RG16S;
        case Tegra::RenderTargetFormat::R16_FLOAT:
            return PixelFormat::R16F;
        case Tegra::RenderTargetFormat::R32_FLOAT:
            return PixelFormat::R32F;
        default:
            LOG_CRITICAL(HW_GPU, "Unimplemented format={}", static_cast<u32>(format));
            UNREACHABLE();
        }
    }

    static PixelFormat PixelFormatFromTextureFormat(Tegra::Texture::TextureFormat format,
                                                    Tegra::Texture::ComponentType component_type) {
        // TODO(Subv): Properly implement this
        switch (format) {
        case Tegra::Texture::TextureFormat::A8R8G8B8:
            return PixelFormat::ABGR8;
        case Tegra::Texture::TextureFormat::B5G6R5:
            return PixelFormat::B5G6R5;
        case Tegra::Texture::TextureFormat::A2B10G10R10:
            return PixelFormat::A2B10G10R10;
        case Tegra::Texture::TextureFormat::A1B5G5R5:
            return PixelFormat::A1B5G5R5;
        case Tegra::Texture::TextureFormat::R8:
            return PixelFormat::R8;
        case Tegra::Texture::TextureFormat::G8R8:
            return PixelFormat::G8R8;
        case Tegra::Texture::TextureFormat::R16_G16_B16_A16:
            return PixelFormat::RGBA16F;
        case Tegra::Texture::TextureFormat::BF10GF11RF11:
            return PixelFormat::R11FG11FB10F;
        case Tegra::Texture::TextureFormat::R32_G32_B32_A32:
            switch (component_type) {
            case Tegra::Texture::ComponentType::FLOAT:
                return PixelFormat::RGBA32F;
            case Tegra::Texture::ComponentType::UINT:
                return PixelFormat::RGBA32UI;
            }
            LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}",
                         static_cast<u32>(component_type));
            UNREACHABLE();
        case Tegra::Texture::TextureFormat::R32_G32:
            return PixelFormat::RG32F;
        case Tegra::Texture::TextureFormat::R32_G32_B32:
            return PixelFormat::RGB32F;
        case Tegra::Texture::TextureFormat::R16:
            switch (component_type) {
            case Tegra::Texture::ComponentType::FLOAT:
                return PixelFormat::R16F;
            case Tegra::Texture::ComponentType::UNORM:
                return PixelFormat::R16UNORM;
            }
            LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}",
                         static_cast<u32>(component_type));
            UNREACHABLE();
        case Tegra::Texture::TextureFormat::R32:
            return PixelFormat::R32F;
        case Tegra::Texture::TextureFormat::ZF32:
            return PixelFormat::Z32F;
        case Tegra::Texture::TextureFormat::Z24S8:
            return PixelFormat::Z24S8;
        case Tegra::Texture::TextureFormat::DXT1:
            return PixelFormat::DXT1;
        case Tegra::Texture::TextureFormat::DXT23:
            return PixelFormat::DXT23;
        case Tegra::Texture::TextureFormat::DXT45:
            return PixelFormat::DXT45;
        case Tegra::Texture::TextureFormat::DXN1:
            return PixelFormat::DXN1;
        case Tegra::Texture::TextureFormat::BC7U:
            return PixelFormat::BC7U;
        case Tegra::Texture::TextureFormat::ASTC_2D_4X4:
            return PixelFormat::ASTC_2D_4X4;
        case Tegra::Texture::TextureFormat::R16_G16:
            switch (component_type) {
            case Tegra::Texture::ComponentType::FLOAT:
                return PixelFormat::RG16F;
            case Tegra::Texture::ComponentType::UNORM:
                return PixelFormat::RG16;
            case Tegra::Texture::ComponentType::SNORM:
                return PixelFormat::RG16S;
            case Tegra::Texture::ComponentType::UINT:
                return PixelFormat::RG16UI;
            case Tegra::Texture::ComponentType::SINT:
                return PixelFormat::RG16I;
            }
            LOG_CRITICAL(HW_GPU, "Unimplemented component_type={}",
                         static_cast<u32>(component_type));
            UNREACHABLE();
        default:
            LOG_CRITICAL(HW_GPU, "Unimplemented format={}, component_type={}",
                         static_cast<u32>(format), static_cast<u32>(component_type));
            UNREACHABLE();
        }
    }

    static Tegra::Texture::TextureFormat TextureFormatFromPixelFormat(PixelFormat format) {
        // TODO(Subv): Properly implement this
        switch (format) {
        case PixelFormat::ABGR8:
        case PixelFormat::SRGBA8:
            return Tegra::Texture::TextureFormat::A8R8G8B8;
        case PixelFormat::B5G6R5:
            return Tegra::Texture::TextureFormat::B5G6R5;
        case PixelFormat::A2B10G10R10:
            return Tegra::Texture::TextureFormat::A2B10G10R10;
        case PixelFormat::A1B5G5R5:
            return Tegra::Texture::TextureFormat::A1B5G5R5;
        case PixelFormat::R8:
            return Tegra::Texture::TextureFormat::R8;
        case PixelFormat::G8R8:
            return Tegra::Texture::TextureFormat::G8R8;
        case PixelFormat::RGBA16F:
            return Tegra::Texture::TextureFormat::R16_G16_B16_A16;
        case PixelFormat::R11FG11FB10F:
            return Tegra::Texture::TextureFormat::BF10GF11RF11;
        case PixelFormat::RGBA32UI:
            return Tegra::Texture::TextureFormat::R32_G32_B32_A32;
        case PixelFormat::DXT1:
            return Tegra::Texture::TextureFormat::DXT1;
        case PixelFormat::DXT23:
            return Tegra::Texture::TextureFormat::DXT23;
        case PixelFormat::DXT45:
            return Tegra::Texture::TextureFormat::DXT45;
        case PixelFormat::DXN1:
            return Tegra::Texture::TextureFormat::DXN1;
        case PixelFormat::BC7U:
            return Tegra::Texture::TextureFormat::BC7U;
        case PixelFormat::ASTC_2D_4X4:
            return Tegra::Texture::TextureFormat::ASTC_2D_4X4;
        case PixelFormat::BGRA8:
            // TODO(bunnei): This is fine for unswizzling (since we just need the right component
            // sizes), but could be a bug if we used this function in different ways.
            return Tegra::Texture::TextureFormat::A8R8G8B8;
        case PixelFormat::RGBA32F:
            return Tegra::Texture::TextureFormat::R32_G32_B32_A32;
        case PixelFormat::RGB32F:
            return Tegra::Texture::TextureFormat::R32_G32_B32;
        case PixelFormat::RG32F:
            return Tegra::Texture::TextureFormat::R32_G32;
        case PixelFormat::R32F:
            return Tegra::Texture::TextureFormat::R32;
        case PixelFormat::R16F:
        case PixelFormat::R16UNORM:
            return Tegra::Texture::TextureFormat::R16;
        case PixelFormat::Z32F:
            return Tegra::Texture::TextureFormat::ZF32;
        case PixelFormat::Z24S8:
            return Tegra::Texture::TextureFormat::Z24S8;
        case PixelFormat::RG16F:
        case PixelFormat::RG16:
        case PixelFormat::RG16UI:
        case PixelFormat::RG16I:
        case PixelFormat::RG16S:
            return Tegra::Texture::TextureFormat::R16_G16;
        default:
            LOG_CRITICAL(HW_GPU, "Unimplemented format={}", static_cast<u32>(format));
            UNREACHABLE();
        }
    }

    static Tegra::DepthFormat DepthFormatFromPixelFormat(PixelFormat format) {
        switch (format) {
        case PixelFormat::S8Z24:
            return Tegra::DepthFormat::S8_Z24_UNORM;
        case PixelFormat::Z24S8:
            return Tegra::DepthFormat::Z24_S8_UNORM;
        case PixelFormat::Z32F:
            return Tegra::DepthFormat::Z32_FLOAT;
        case PixelFormat::Z16:
            return Tegra::DepthFormat::Z16_UNORM;
        case PixelFormat::Z32FS8:
            return Tegra::DepthFormat::Z32_S8_X24_FLOAT;
        default:
            LOG_CRITICAL(HW_GPU, "Unimplemented format={}", static_cast<u32>(format));
            UNREACHABLE();
        }
    }

    static ComponentType ComponentTypeFromTexture(Tegra::Texture::ComponentType type) {
        // TODO(Subv): Implement more component types
        switch (type) {
        case Tegra::Texture::ComponentType::UNORM:
            return ComponentType::UNorm;
        case Tegra::Texture::ComponentType::FLOAT:
            return ComponentType::Float;
        case Tegra::Texture::ComponentType::SNORM:
            return ComponentType::SNorm;
        case Tegra::Texture::ComponentType::UINT:
            return ComponentType::UInt;
        case Tegra::Texture::ComponentType::SINT:
            return ComponentType::SInt;
        default:
            LOG_CRITICAL(HW_GPU, "Unimplemented component type={}", static_cast<u32>(type));
            UNREACHABLE();
        }
    }

    static ComponentType ComponentTypeFromRenderTarget(Tegra::RenderTargetFormat format) {
        // TODO(Subv): Implement more render targets
        switch (format) {
        case Tegra::RenderTargetFormat::RGBA8_UNORM:
        case Tegra::RenderTargetFormat::RGBA8_SRGB:
        case Tegra::RenderTargetFormat::BGRA8_UNORM:
        case Tegra::RenderTargetFormat::RGB10_A2_UNORM:
        case Tegra::RenderTargetFormat::R8_UNORM:
        case Tegra::RenderTargetFormat::RG16_UNORM:
        case Tegra::RenderTargetFormat::B5G6R5_UNORM:
            return ComponentType::UNorm;
        case Tegra::RenderTargetFormat::RG16_SNORM:
            return ComponentType::SNorm;
        case Tegra::RenderTargetFormat::RGBA16_FLOAT:
        case Tegra::RenderTargetFormat::R11G11B10_FLOAT:
        case Tegra::RenderTargetFormat::RGBA32_FLOAT:
        case Tegra::RenderTargetFormat::RG32_FLOAT:
        case Tegra::RenderTargetFormat::RG16_FLOAT:
        case Tegra::RenderTargetFormat::R16_FLOAT:
        case Tegra::RenderTargetFormat::R32_FLOAT:
            return ComponentType::Float;
        case Tegra::RenderTargetFormat::RGBA32_UINT:
        case Tegra::RenderTargetFormat::RG16_UINT:
            return ComponentType::UInt;
        case Tegra::RenderTargetFormat::RG16_SINT:
            return ComponentType::SInt;
        default:
            LOG_CRITICAL(HW_GPU, "Unimplemented format={}", static_cast<u32>(format));
            UNREACHABLE();
        }
    }

    static PixelFormat PixelFormatFromGPUPixelFormat(Tegra::FramebufferConfig::PixelFormat format) {
        switch (format) {
        case Tegra::FramebufferConfig::PixelFormat::ABGR8:
            return PixelFormat::ABGR8;
        default:
            LOG_CRITICAL(HW_GPU, "Unimplemented format={}", static_cast<u32>(format));
            UNREACHABLE();
        }
    }

    static ComponentType ComponentTypeFromDepthFormat(Tegra::DepthFormat format) {
        switch (format) {
        case Tegra::DepthFormat::Z16_UNORM:
        case Tegra::DepthFormat::S8_Z24_UNORM:
        case Tegra::DepthFormat::Z24_S8_UNORM:
            return ComponentType::UNorm;
        case Tegra::DepthFormat::Z32_FLOAT:
        case Tegra::DepthFormat::Z32_S8_X24_FLOAT:
            return ComponentType::Float;
        default:
            LOG_CRITICAL(HW_GPU, "Unimplemented format={}", static_cast<u32>(format));
            UNREACHABLE();
        }
    }

    static SurfaceType GetFormatType(PixelFormat pixel_format) {
        if (static_cast<size_t>(pixel_format) < static_cast<size_t>(PixelFormat::MaxColorFormat)) {
            return SurfaceType::ColorTexture;
        }

        if (static_cast<size_t>(pixel_format) <
            static_cast<size_t>(PixelFormat::MaxDepthStencilFormat)) {
            return SurfaceType::DepthStencil;
        }

        // TODO(Subv): Implement the other formats
        ASSERT(false);

        return SurfaceType::Invalid;
    }

    /// Returns the rectangle corresponding to this surface
    MathUtil::Rectangle<u32> GetRect() const;

    /// Returns the size of this surface in bytes, adjusted for compression
    size_t SizeInBytes() const {
        const u32 compression_factor{GetCompressionFactor(pixel_format)};
        ASSERT(width % compression_factor == 0);
        ASSERT(height % compression_factor == 0);
        return (width / compression_factor) * (height / compression_factor) *
               GetFormatBpp(pixel_format) / CHAR_BIT;
    }

    /// Returns the CPU virtual address for this surface
    VAddr GetCpuAddr() const;

    /// Returns true if the specified region overlaps with this surface's region in Switch memory
    bool IsOverlappingRegion(Tegra::GPUVAddr region_addr, size_t region_size) const {
        return addr <= (region_addr + region_size) && region_addr <= (addr + size_in_bytes);
    }

    /// Creates SurfaceParams from a texture configuration
    static SurfaceParams CreateForTexture(const Tegra::Texture::FullTextureInfo& config);

    /// Creates SurfaceParams from a framebuffer configuration
    static SurfaceParams CreateForFramebuffer(
        const Tegra::Engines::Maxwell3D::Regs::RenderTargetConfig& config);

    /// Creates SurfaceParams for a depth buffer configuration
    static SurfaceParams CreateForDepthBuffer(u32 zeta_width, u32 zeta_height,
                                              Tegra::GPUVAddr zeta_address,
                                              Tegra::DepthFormat format);

    bool operator==(const SurfaceParams& other) const {
        return std::tie(addr, is_tiled, block_height, pixel_format, component_type, type, width,
                        height, unaligned_height, size_in_bytes) ==
               std::tie(other.addr, other.is_tiled, other.block_height, other.pixel_format,
                        other.component_type, other.type, other.width, other.height,
                        other.unaligned_height, other.size_in_bytes);
    }

    bool operator!=(const SurfaceParams& other) const {
        return !operator==(other);
    }

    /// Checks if surfaces are compatible for caching
    bool IsCompatibleSurface(const SurfaceParams& other) const {
        return std::tie(pixel_format, type, cache_width, cache_height) ==
               std::tie(other.pixel_format, other.type, other.cache_width, other.cache_height);
    }

    Tegra::GPUVAddr addr;
    bool is_tiled;
    u32 block_height;
    PixelFormat pixel_format;
    ComponentType component_type;
    SurfaceType type;
    u32 width;
    u32 height;
    u32 unaligned_height;
    size_t size_in_bytes;

    // Parameters used for caching only
    u32 cache_width;
    u32 cache_height;
};

class CachedSurface final {
public:
    CachedSurface(const SurfaceParams& params);

    const OGLTexture& Texture() const {
        return texture;
    }

    static constexpr unsigned int GetGLBytesPerPixel(SurfaceParams::PixelFormat format) {
        if (format == SurfaceParams::PixelFormat::Invalid)
            return 0;

        return SurfaceParams::GetFormatBpp(format) / CHAR_BIT;
    }

    const SurfaceParams& GetSurfaceParams() const {
        return params;
    }

    // Read/Write data in Switch memory to/from gl_buffer
    void LoadGLBuffer();
    void FlushGLBuffer();

    // Upload/Download data in gl_buffer in/to this surface's texture
    void UploadGLTexture(GLuint read_fb_handle, GLuint draw_fb_handle);
    void DownloadGLTexture(GLuint read_fb_handle, GLuint draw_fb_handle);

private:
    OGLTexture texture;
    std::vector<u8> gl_buffer;
    SurfaceParams params;
};

class RasterizerCacheOpenGL final : NonCopyable {
public:
    RasterizerCacheOpenGL();
    ~RasterizerCacheOpenGL();

    /// Get a surface based on the texture configuration
    Surface GetTextureSurface(const Tegra::Texture::FullTextureInfo& config);

    /// Get the color and depth surfaces based on the framebuffer configuration
    SurfaceSurfaceRect_Tuple GetFramebufferSurfaces(bool using_color_fb, bool using_depth_fb,
                                                    const MathUtil::Rectangle<s32>& viewport);

    /// Flushes the surface to Switch memory
    void FlushSurface(const Surface& surface);

    /// Tries to find a framebuffer GPU address based on the provided CPU address
    Surface TryFindFramebufferSurface(VAddr cpu_addr) const;

    /// Write any cached resources overlapping the region back to memory (if dirty)
    void FlushRegion(Tegra::GPUVAddr addr, size_t size);

    /// Mark the specified region as being invalidated
    void InvalidateRegion(Tegra::GPUVAddr addr, size_t size);

private:
    void LoadSurface(const Surface& surface);
    Surface GetSurface(const SurfaceParams& params);

    /// Recreates a surface with new parameters
    Surface RecreateSurface(const Surface& surface, const SurfaceParams& new_params);

    /// Register surface into the cache
    void RegisterSurface(const Surface& surface);

    /// Remove surface from the cache
    void UnregisterSurface(const Surface& surface);

    /// Increase/decrease the number of surface in pages touching the specified region
    void UpdatePagesCachedCount(Tegra::GPUVAddr addr, u64 size, int delta);

    std::unordered_map<Tegra::GPUVAddr, Surface> surface_cache;
    PageMap cached_pages;

    OGLFramebuffer read_framebuffer;
    OGLFramebuffer draw_framebuffer;
};
