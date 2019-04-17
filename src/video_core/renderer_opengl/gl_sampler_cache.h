// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <glad/glad.h>

#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/sampler_cache.h"

namespace OpenGL {

class SamplerCacheOpenGL final : public VideoCommon::SamplerCache<GLuint, OGLSampler> {
public:
    explicit SamplerCacheOpenGL();
    ~SamplerCacheOpenGL();

protected:
    OGLSampler CreateSampler(const Tegra::Texture::TSCEntry& tsc) const;

    GLuint ToSamplerType(const OGLSampler& sampler) const;
};

} // namespace OpenGL
