// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <unordered_map>

#include <glad/glad.h>

#include "common/common_types.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_state.h"

namespace OpenGL {

class FramebufferCacheOpenGLImpl;
using FramebufferCacheOpenGL = std::shared_ptr<FramebufferCacheOpenGLImpl>;

struct alignas(sizeof(u64)) FramebufferCacheKey {
    bool is_single_buffer = false;
    bool stencil_enable = false;

    std::array<GLenum, Tegra::Engines::Maxwell3D::Regs::NumRenderTargets> color_attachments{};
    std::array<GLuint, Tegra::Engines::Maxwell3D::Regs::NumRenderTargets> colors{};
    u32 colors_count = 0;

    GLuint zeta = 0;

    std::size_t Hash() const;

    bool operator==(const FramebufferCacheKey& rhs) const;

    bool operator!=(const FramebufferCacheKey& rhs) const {
        return !operator==(rhs);
    }
};

} // namespace OpenGL

namespace std {

template <>
struct hash<OpenGL::FramebufferCacheKey> {
    std::size_t operator()(const OpenGL::FramebufferCacheKey& k) const noexcept {
        return k.Hash();
    }
};

} // namespace std

namespace OpenGL {

class FramebufferCacheOpenGLImpl {
public:
    FramebufferCacheOpenGLImpl();
    ~FramebufferCacheOpenGLImpl();

    /// Returns and caches a framebuffer with the passed arguments.
    GLuint GetFramebuffer(const FramebufferCacheKey& key);

    /// Invalidates a texture inside the cache.
    void InvalidateTexture(GLuint texture);

private:
    using CacheType = std::unordered_map<FramebufferCacheKey, OGLFramebuffer>;

    /// Returns a new framebuffer from the passed arguments.
    OGLFramebuffer CreateFramebuffer(const FramebufferCacheKey& key);

    /// Attempts to destroy the framebuffer cache entry in `it` and returns the next one.
    CacheType::iterator TryTextureInvalidation(GLuint texture, CacheType::iterator it);

    OpenGLState local_state;
    CacheType cache;
};

} // namespace OpenGL
