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

FramebufferCacheOpenGL::FramebufferCacheOpenGL() = default;

FramebufferCacheOpenGL::~FramebufferCacheOpenGL() = default;

GLuint FramebufferCacheOpenGL::GetFramebuffer(const FramebufferCacheKey& key) {
    const auto [entry, is_cache_miss] = cache.try_emplace(key);
    auto& framebuffer{entry->second};
    if (is_cache_miss) {
        framebuffer = CreateFramebuffer(key);
    }
    return framebuffer.handle;
}

OGLFramebuffer FramebufferCacheOpenGL::CreateFramebuffer(const FramebufferCacheKey& key) {
    OGLFramebuffer framebuffer;
    framebuffer.Create();

    // TODO(Rodrigo): Use DSA here after Nvidia fixes their framebuffer DSA bugs.
    local_state.draw.draw_framebuffer = framebuffer.handle;
    local_state.ApplyFramebufferState();

    if (key.is_single_buffer) {
        if (key.color_attachments[0] != GL_NONE && key.colors[0]) {
            key.colors[0]->Attach(key.color_attachments[0], GL_DRAW_FRAMEBUFFER);
            glDrawBuffer(key.color_attachments[0]);
        } else {
            glDrawBuffer(GL_NONE);
        }
    } else {
        for (std::size_t index = 0; index < Maxwell::NumRenderTargets; ++index) {
            if (key.colors[index]) {
                key.colors[index]->Attach(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(index),
                                          GL_DRAW_FRAMEBUFFER);
            }
        }
        glDrawBuffers(key.colors_count, key.color_attachments.data());
    }

    if (key.zeta) {
        key.zeta->Attach(key.stencil_enable ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
                         GL_DRAW_FRAMEBUFFER);
    }

    return framebuffer;
}

std::size_t FramebufferCacheKey::Hash() const {
    static_assert(sizeof(*this) % sizeof(u64) == 0, "Unaligned struct");
    return static_cast<std::size_t>(
        Common::CityHash64(reinterpret_cast<const char*>(this), sizeof(*this)));
}

bool FramebufferCacheKey::operator==(const FramebufferCacheKey& rhs) const {
    return std::tie(is_single_buffer, stencil_enable, colors_count, color_attachments, colors,
                    zeta) == std::tie(rhs.is_single_buffer, rhs.stencil_enable, rhs.colors_count,
                                      rhs.color_attachments, rhs.colors, rhs.zeta);
}

} // namespace OpenGL
