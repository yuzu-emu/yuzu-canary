// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glad/glad.h>

#include "common/common_types.h"
#include "video_core/engines/shader_bytecode.h"
#include "video_core/renderer_opengl/gl_device.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/texture_cache/texture_cache.h"

namespace OpenGL {

using VideoCommon::SurfaceParams;
using VideoCommon::ViewParams;

class CachedSurfaceView;
class CachedSurface;
class TextureCacheOpenGL;

using Surface = std::shared_ptr<CachedSurface>;
using View = std::shared_ptr<CachedSurfaceView>;
using TextureCacheBase = VideoCommon::TextureCache<Surface, View>;

class CachedSurface final : public VideoCommon::SurfaceBase<View> {
    friend CachedSurfaceView;

public:
    explicit CachedSurface(GPUVAddr gpu_addr, const SurfaceParams& params);
    ~CachedSurface();

    void Init();

    void UploadTexture(const std::vector<u8>& staging_buffer) override;
    void DownloadTexture(std::vector<u8>& staging_buffer) override;

    GLenum GetTarget() const {
        return target;
    }

    GLuint GetTexture() const {
        return texture.handle;
    }

protected:
    void DecorateSurfaceName() override;

    View CreateView(const ViewParams& view_key) override;
    View CreateViewInner(const ViewParams& view_key, bool is_proxy);

private:
    void UploadTextureMipmap(u32 level, const std::vector<u8>& staging_buffer);

    GLenum internal_format{};
    GLenum format{};
    GLenum type{};
    bool is_compressed{};
    GLenum target{};
    u32 view_count{};

    OGLTexture texture;
    OGLBuffer texture_buffer;
};

class CachedSurfaceView final : public VideoCommon::ViewBase {
public:
    explicit CachedSurfaceView(CachedSurface& surface, const ViewParams& params, bool is_proxy);
    ~CachedSurfaceView();

    /// Attaches this texture view to the current bound GL_DRAW_FRAMEBUFFER
    void Attach(GLenum attachment, GLenum target) const;

    void ApplySwizzle(Tegra::Texture::SwizzleSource x_source,
                      Tegra::Texture::SwizzleSource y_source,
                      Tegra::Texture::SwizzleSource z_source,
                      Tegra::Texture::SwizzleSource w_source);

    void DecorateViewName(GPUVAddr gpu_addr, std::string prefix);

    void MarkAsModified(u64 tick) {
        surface.MarkAsModified(true, tick);
    }

    GLuint GetTexture() const {
        if (is_proxy) {
            return surface.GetTexture();
        }
        return texture_view.handle;
    }

    const CachedSurface& GetParent() const {
        return surface;
    }

    const SurfaceParams& GetSurfaceParams() const {
        return surface.GetSurfaceParams();
    }

private:
    u32 EncodeSwizzle(Tegra::Texture::SwizzleSource x_source,
                      Tegra::Texture::SwizzleSource y_source,
                      Tegra::Texture::SwizzleSource z_source,
                      Tegra::Texture::SwizzleSource w_source) const {
        return (static_cast<u32>(x_source) << 24) | (static_cast<u32>(y_source) << 16) |
               (static_cast<u32>(z_source) << 8) | static_cast<u32>(w_source);
    }

    OGLTextureView CreateTextureView() const;

    CachedSurface& surface;
    GLenum target{};

    OGLTextureView texture_view;
    u32 swizzle{};
    bool is_proxy{};
};

class TextureCacheOpenGL final : public TextureCacheBase {
public:
    explicit TextureCacheOpenGL(Core::System& system, VideoCore::RasterizerInterface& rasterizer,
                                const Device& device);
    ~TextureCacheOpenGL();

protected:
    Surface CreateSurface(GPUVAddr gpu_addr, const SurfaceParams& params) override;

    void ImageCopy(Surface& src_surface, Surface& dst_surface,
                   const VideoCommon::CopyParams& copy_params) override;

    void ImageBlit(View& src_view, View& dst_view,
                   const Tegra::Engines::Fermi2D::Config& copy_config) override;

    void BufferCopy(Surface& src_surface, Surface& dst_surface) override;

private:
    GLuint FetchPBO(std::size_t buffer_size);

    OGLFramebuffer src_framebuffer;
    OGLFramebuffer dst_framebuffer;
    std::unordered_map<u32, OGLBuffer> copy_pbo_cache;
};

} // namespace OpenGL
