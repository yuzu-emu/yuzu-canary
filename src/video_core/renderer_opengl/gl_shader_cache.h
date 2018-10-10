// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <map>
#include <memory>

#include "common/assert.h"
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
    std::size_t GetSizeInBytes() const {
        return GLShader::MAX_PROGRAM_CODE_LENGTH * sizeof(u64);
    }

    /// Gets the shader entries for the shader
    const GLShader::ShaderEntries& GetShaderEntries() const {
        return entries;
    }

    /// Gets the GL program handle for the shader
    GLuint GetProgramHandle(GLenum primitive_mode) {
        if (program_type != Maxwell::ShaderProgram::Geometry) {
            return program.handle;
        }
        switch (primitive_mode) {
        case GL_POINTS:
            return LazyGeometryProgram(geometry_programs.points, "points", "ShaderPoints");
        case GL_LINES:
        case GL_LINE_STRIP:
            return LazyGeometryProgram(geometry_programs.lines, "lines", "ShaderLines");
        case GL_LINES_ADJACENCY:
        case GL_LINE_STRIP_ADJACENCY:
            return LazyGeometryProgram(geometry_programs.lines_adjacency, "lines_adjacency",
                                       "ShaderLinesAdjacency");
        case GL_TRIANGLES:
        case GL_TRIANGLE_STRIP:
        case GL_TRIANGLE_FAN:
            return LazyGeometryProgram(geometry_programs.triangles, "triangles", "ShaderTriangles");
        case GL_TRIANGLES_ADJACENCY:
        case GL_TRIANGLE_STRIP_ADJACENCY:
            return LazyGeometryProgram(geometry_programs.triangles_adjacency, "triangles_adjacency",
                                       "ShaderLines");
        default:
            UNREACHABLE_MSG("Unknown primitive mode.");
        }
    }

    /// Gets the GL program resource location for the specified resource, caching as needed
    GLuint GetProgramResourceIndex(const GLShader::ConstBufferEntry& buffer);

    /// Gets the GL uniform location for the specified resource, caching as needed
    GLint GetUniformLocation(const GLShader::SamplerEntry& sampler);

private:
    /// Generates a geometry shader or returns one that already exists.
    GLuint LazyGeometryProgram(OGLProgram& target_program, const std::string& glsl_topology,
                               const std::string& debug_name);

    VAddr addr;
    Maxwell::ShaderProgram program_type;
    GLShader::ShaderSetup setup;
    GLShader::ShaderEntries entries;

    // Non-geometry program.
    OGLProgram program;

    // Geometry programs. These are needed because GLSL needs an input topology but it's not
    // declared by the hardware. Workaround this issue by generating a different shader per input
    // topology class.
    struct {
        std::string code;
        OGLProgram points;
        OGLProgram lines;
        OGLProgram lines_adjacency;
        OGLProgram triangles;
        OGLProgram triangles_adjacency;
    } geometry_programs;

    std::map<u32, GLuint> resource_cache;
    std::map<u32, GLint> uniform_cache;
};

class ShaderCacheOpenGL final : public RasterizerCache<Shader> {
public:
    /// Gets the current specified shader stage program
    Shader GetStageProgram(Maxwell::ShaderProgram program);
};

} // namespace OpenGL
