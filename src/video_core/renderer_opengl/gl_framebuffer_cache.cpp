// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <tuple>

#include "common/cityhash.h"
#include "common/scope_exit.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_opengl/gl_framebuffer_cache.h"
#include "video_core/renderer_opengl/gl_state.h"

namespace OpenGL {

using Maxwell = Tegra::Engines::Maxwell3D::Regs;

std::size_t FramebufferCacheKey::Hash() const {
    static_assert(sizeof(*this) % sizeof(u64) == 0, "Unaligned struct");
    return static_cast<std::size_t>(
        Common::CityHash64(reinterpret_cast<const char*>(this), sizeof(*this)));
}

bool FramebufferCacheKey::operator==(const FramebufferCacheKey& rhs) const {
    return std::tie(is_single_buffer, stencil_enable, color_attachments, colors, colors_count,
                    zeta) == std::tie(rhs.is_single_buffer, rhs.stencil_enable,
                                      rhs.color_attachments, rhs.colors, rhs.colors_count,
                                      rhs.zeta);
}

FramebufferCacheOpenGLImpl::FramebufferCacheOpenGLImpl() = default;

FramebufferCacheOpenGLImpl::~FramebufferCacheOpenGLImpl() = default;

GLuint FramebufferCacheOpenGLImpl::GetFramebuffer(const FramebufferCacheKey& key) {
    const auto [entry, is_cache_miss] = cache.try_emplace(key);
    auto& framebuffer{entry->second};
    if (is_cache_miss) {
        framebuffer = CreateFramebuffer(key);
    }
    return framebuffer.handle;
}

void FramebufferCacheOpenGLImpl::InvalidateTexture(GLuint texture) {
    for (auto it = cache.begin(); it != cache.end();) {
        it = TryTextureInvalidation(texture, it);
    }
}

OGLFramebuffer FramebufferCacheOpenGLImpl::CreateFramebuffer(const FramebufferCacheKey& key) {
    OGLFramebuffer framebuffer;
    framebuffer.Create();

    // TODO(Rodrigo): Use DSA here after Nvidia fixes their framebuffer DSA bugs.
    local_state.draw.draw_framebuffer = framebuffer.handle;
    local_state.ApplyFramebufferState();

    if (key.is_single_buffer) {
        if (key.color_attachments[0] != GL_NONE) {
            glFramebufferTexture(GL_DRAW_FRAMEBUFFER, key.color_attachments[0], key.colors[0], 0);
        }
        glDrawBuffer(key.color_attachments[0]);
    } else {
        for (std::size_t index = 0; index < Maxwell::NumRenderTargets; ++index) {
            if (key.colors[index]) {
                glFramebufferTexture(GL_DRAW_FRAMEBUFFER,
                                     GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(index),
                                     key.colors[index], 0);
            }
        }
        glDrawBuffers(key.colors_count, key.color_attachments.data());
    }

    if (key.zeta) {
        const GLenum zeta_attachment =
            key.stencil_enable ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
        glFramebufferTexture(GL_DRAW_FRAMEBUFFER, zeta_attachment, key.zeta, 0);
    }

    return framebuffer;
}

FramebufferCacheOpenGLImpl::CacheType::iterator FramebufferCacheOpenGLImpl::TryTextureInvalidation(
    GLuint texture, CacheType::iterator it) {
    const auto& [key, framebuffer] = *it;
    const std::size_t count = key.is_single_buffer ? 1 : static_cast<std::size_t>(key.colors_count);
    for (std::size_t i = 0; i < count; ++i) {
        if (texture == key.colors[i]) {
            return cache.erase(it);
        }
    }
    if (texture == key.zeta) {
        return cache.erase(it);
    }
    return ++it;
}

} // namespace OpenGL
