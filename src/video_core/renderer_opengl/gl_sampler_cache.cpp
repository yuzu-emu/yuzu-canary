// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_sampler_cache.h"
#include "video_core/renderer_opengl/maxwell_to_gl.h"

namespace OpenGL {

SamplerCacheOpenGL::SamplerCacheOpenGL() = default;

SamplerCacheOpenGL::~SamplerCacheOpenGL() = default;

OGLSampler SamplerCacheOpenGL::CreateSampler(const Tegra::Texture::TSCEntry& tsc) const {
    OGLSampler sampler;
    sampler.Create();

    const GLuint sampler_id{sampler.handle};
    glSamplerParameteri(
        sampler_id, GL_TEXTURE_MAG_FILTER,
        MaxwellToGL::TextureFilterMode(tsc.mag_filter, Tegra::Texture::TextureMipmapFilter::None));
    glSamplerParameteri(sampler_id, GL_TEXTURE_MIN_FILTER,
                        MaxwellToGL::TextureFilterMode(tsc.min_filter, tsc.mipmap_filter));
    glSamplerParameteri(sampler_id, GL_TEXTURE_WRAP_S, MaxwellToGL::WrapMode(tsc.wrap_u));
    glSamplerParameteri(sampler_id, GL_TEXTURE_WRAP_T, MaxwellToGL::WrapMode(tsc.wrap_v));
    glSamplerParameteri(sampler_id, GL_TEXTURE_WRAP_R, MaxwellToGL::WrapMode(tsc.wrap_p));
    glSamplerParameteri(sampler_id, GL_TEXTURE_COMPARE_MODE,
                        tsc.depth_compare_enabled == 1 ? GL_COMPARE_REF_TO_TEXTURE : GL_NONE);
    glSamplerParameteri(sampler_id, GL_TEXTURE_COMPARE_FUNC,
                        MaxwellToGL::DepthCompareFunc(tsc.depth_compare_func));
    glSamplerParameterfv(sampler_id, GL_TEXTURE_BORDER_COLOR, tsc.GetBorderColor().data());
    glSamplerParameterf(sampler_id, GL_TEXTURE_MIN_LOD, tsc.GetMinLod());
    glSamplerParameterf(sampler_id, GL_TEXTURE_MAX_LOD, tsc.GetMaxLod());
    glSamplerParameterf(sampler_id, GL_TEXTURE_LOD_BIAS, tsc.GetLodBias());
    if (GLAD_GL_ARB_texture_filter_anisotropic) {
        glSamplerParameterf(sampler_id, GL_TEXTURE_MAX_ANISOTROPY, tsc.GetMaxAnisotropy());
    } else if (GLAD_GL_EXT_texture_filter_anisotropic) {
        glSamplerParameterf(sampler_id, GL_TEXTURE_MAX_ANISOTROPY_EXT, tsc.GetMaxAnisotropy());
    } else if (tsc.GetMaxAnisotropy() != 1) {
        LOG_WARNING(Render_OpenGL, "Anisotropy not supported by host GPU driver");
    }

    return sampler;
}

GLuint SamplerCacheOpenGL::ToSamplerType(const OGLSampler& sampler) const {
    return sampler.handle;
}

} // namespace OpenGL
