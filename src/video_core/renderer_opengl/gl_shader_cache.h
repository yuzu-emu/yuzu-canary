// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <map>
#include <memory>

#include "common/common_types.h"
#include "video_core/rasterizer_cache.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_shader_gen.h"

namespace OpenGL {

class CachedShader;
using Shader = std::shared_ptr<CachedShader>;
using Maxwell = Tegra::Engines::Maxwell3D::Regs;

class CachedShader final {
public:
    CachedShader(VAddr addr, Maxwell::ShaderProgram program_type);

    /// Gets the address of the shader in guest memory, required for cache management
    VAddr GetAddr() const {
        return addr;
    }

    /// Gets the size of the shader in guest memory, required for cache management
    size_t GetSizeInBytes() const {
        return GLShader::MAX_PROGRAM_CODE_LENGTH * sizeof(u64);
    }

    /// Gets the shader entries for the shader
    const GLShader::ShaderEntries& GetShaderEntries() const {
        return entries;
    }

    /// Gets the GL program handle for the shader
    GLuint GetProgramHandle() const {
        return program.handle;
    }

    /// Gets the GL program resource location for the specified resource, caching as needed
    GLuint GetProgramResourceIndex(const GLShader::ConstBufferEntry& buffer);

    /// Gets the GL uniform location for the specified resource, caching as needed
    GLint GetUniformLocation(const GLShader::SamplerEntry& sampler);

private:
    VAddr addr;
    Maxwell::ShaderProgram program_type;
    GLShader::ShaderSetup setup;
    GLShader::ShaderEntries entries;
    OGLProgram program;

    std::map<u32, GLuint> resource_cache;
    std::map<u32, GLint> uniform_cache;
};

class ShaderCacheOpenGL final : public RasterizerCache<Shader> {
public:
    /// Gets the current specified shader stage program
    Shader GetStageProgram(Maxwell::ShaderProgram program);
};

} // namespace OpenGL
