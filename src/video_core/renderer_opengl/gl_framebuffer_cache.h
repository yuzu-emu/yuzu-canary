// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <unordered_map>

#include <glad/glad.h>

#include "common/common_types.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_state.h"
#include "video_core/renderer_opengl/gl_texture_cache.h"

namespace OpenGL {

struct alignas(sizeof(u64)) FramebufferCacheKey {
    bool stencil_enable = false;
    u16 colors_count = 0;

    std::array<GLenum, Tegra::Engines::Maxwell3D::Regs::NumRenderTargets> color_attachments{};
    std::array<View, Tegra::Engines::Maxwell3D::Regs::NumRenderTargets> colors;
    View zeta;

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

class FramebufferCacheOpenGL {
public:
    FramebufferCacheOpenGL();
    ~FramebufferCacheOpenGL();

    GLuint GetFramebuffer(const FramebufferCacheKey& key);

private:
    OGLFramebuffer CreateFramebuffer(const FramebufferCacheKey& key);

    OpenGLState local_state;
    std::unordered_map<FramebufferCacheKey, OGLFramebuffer> cache;
};

} // namespace OpenGL
