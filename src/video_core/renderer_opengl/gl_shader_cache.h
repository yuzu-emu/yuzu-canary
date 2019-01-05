// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <map>
#include <memory>
#include <tuple>

#include <glad/glad.h>

#include "common/assert.h"
#include "common/common_types.h"
#include "video_core/rasterizer_cache.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_shader_decompiler.h"
#include "video_core/renderer_opengl/gl_shader_gen.h"

namespace OpenGL {

class CachedShader;
class RasterizerOpenGL;

using Shader = std::shared_ptr<CachedShader>;
using Maxwell = Tegra::Engines::Maxwell3D::Regs;

struct BaseBindings {
    u32 cbuf{};
    u32 gmem{};
    u32 sampler{};

    bool operator<(const BaseBindings& rhs) const {
        return std::tie(cbuf, gmem, sampler) < std::tie(rhs.cbuf, rhs.gmem, rhs.sampler);
    }
};

class CachedShader final : public RasterizerCacheObject {
public:
    CachedShader(VAddr addr, Maxwell::ShaderProgram program_type);

    VAddr GetAddr() const override {
        return addr;
    }

    std::size_t GetSizeInBytes() const override {
        return shader_length;
    }

    // We do not have to flush this cache as things in it are never modified by us.
    void Flush() override {}

    /// Gets the shader entries for the shader
    const GLShader::ShaderEntries& GetShaderEntries() const {
        return entries;
    }

    /// Gets the GL program handle for the shader
    std::tuple<GLuint, BaseBindings> GetProgramHandle(GLenum primitive_mode,
                                                      BaseBindings base_bindings);

private:
    // Geometry programs. These are needed because GLSL needs an input topology but it's not
    // declared by the hardware. Workaround this issue by generating a different shader per input
    // topology class.
    struct GeometryPrograms {
        OGLProgram points;
        OGLProgram lines;
        OGLProgram lines_adjacency;
        OGLProgram triangles;
        OGLProgram triangles_adjacency;
    };

    std::string AllocateBindings(BaseBindings base_bindings);

    GLuint GetGeometryShader(GLenum primitive_mode, BaseBindings base_bindings);

    /// Generates a geometry shader or returns one that already exists.
    GLuint LazyGeometryProgram(OGLProgram& target_program, BaseBindings base_bindings,
                               const std::string& glsl_topology, u32 max_vertices,
                               const std::string& debug_name);

    void CalculateProperties();

    VAddr addr{};
    std::size_t shader_length{};
    Maxwell::ShaderProgram program_type{};
    GLShader::ShaderSetup setup;
    GLShader::ShaderEntries entries;

    std::string code;

    std::map<BaseBindings, OGLProgram> programs;
    std::map<BaseBindings, GeometryPrograms> geometry_programs;

    std::map<u32, GLuint> cbuf_resource_cache;
    std::map<u32, GLuint> gmem_resource_cache;
    std::map<u32, GLint> uniform_cache;
};

class ShaderCacheOpenGL final : public RasterizerCache<Shader> {
public:
    explicit ShaderCacheOpenGL(RasterizerOpenGL& rasterizer);

    /// Gets the current specified shader stage program
    Shader GetStageProgram(Maxwell::ShaderProgram program);
};

} // namespace OpenGL
