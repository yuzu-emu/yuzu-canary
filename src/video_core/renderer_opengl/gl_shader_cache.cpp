// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "core/core.h"
#include "core/memory.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/renderer_opengl/gl_shader_cache.h"
#include "video_core/renderer_opengl/gl_shader_manager.h"
#include "video_core/renderer_opengl/utils.h"

namespace OpenGL {

/// Gets the address for the specified shader stage program
static VAddr GetShaderAddress(Maxwell::ShaderProgram program) {
    const auto& gpu = Core::System::GetInstance().GPU().Maxwell3D();
    const auto& shader_config = gpu.regs.shader_config[static_cast<std::size_t>(program)];
    return *gpu.memory_manager.GpuToCpuAddress(gpu.regs.code_address.CodeAddress() +
                                               shader_config.offset);
}

/// Gets the shader program code from memory for the specified address
static GLShader::ProgramCode GetShaderCode(VAddr addr) {
    GLShader::ProgramCode program_code(GLShader::MAX_PROGRAM_CODE_LENGTH);
    Memory::ReadBlock(addr, program_code.data(), program_code.size() * sizeof(u64));
    return program_code;
}

/// Helper function to set shader uniform block bindings for a single shader stage
static void SetShaderUniformBlockBinding(GLuint shader, const char* name,
                                         Maxwell::ShaderStage binding, std::size_t expected_size) {
    const GLuint ub_index = glGetUniformBlockIndex(shader, name);
    if (ub_index == GL_INVALID_INDEX) {
        return;
    }

    GLint ub_size = 0;
    glGetActiveUniformBlockiv(shader, ub_index, GL_UNIFORM_BLOCK_DATA_SIZE, &ub_size);
    ASSERT_MSG(static_cast<std::size_t>(ub_size) == expected_size,
               "Uniform block size did not match! Got {}, expected {}", ub_size, expected_size);
    glUniformBlockBinding(shader, ub_index, static_cast<GLuint>(binding));
}

/// Sets shader uniform block bindings for an entire shader program
static void SetShaderUniformBlockBindings(GLuint shader) {
    SetShaderUniformBlockBinding(shader, "vs_config", Maxwell::ShaderStage::Vertex,
                                 sizeof(GLShader::MaxwellUniformData));
    SetShaderUniformBlockBinding(shader, "gs_config", Maxwell::ShaderStage::Geometry,
                                 sizeof(GLShader::MaxwellUniformData));
    SetShaderUniformBlockBinding(shader, "fs_config", Maxwell::ShaderStage::Fragment,
                                 sizeof(GLShader::MaxwellUniformData));
}

CachedShader::CachedShader(VAddr addr, Maxwell::ShaderProgram program_type)
    : addr{addr}, program_type{program_type}, setup{GetShaderCode(addr)} {

    GLShader::ProgramResult program_result;
    GLenum gl_type{};

    switch (program_type) {
    case Maxwell::ShaderProgram::VertexA:
        // VertexB is always enabled, so when VertexA is enabled, we have two vertex shaders.
        // Conventional HW does not support this, so we combine VertexA and VertexB into one
        // stage here.
        setup.SetProgramB(GetShaderCode(GetShaderAddress(Maxwell::ShaderProgram::VertexB)));
    case Maxwell::ShaderProgram::VertexB:
        program_result = GLShader::GenerateVertexShader(setup);
        gl_type = GL_VERTEX_SHADER;
        break;
    case Maxwell::ShaderProgram::Geometry:
        program_result = GLShader::GenerateGeometryShader(setup);
        gl_type = GL_GEOMETRY_SHADER;
        break;
    case Maxwell::ShaderProgram::Fragment:
        program_result = GLShader::GenerateFragmentShader(setup);
        gl_type = GL_FRAGMENT_SHADER;
        break;
    default:
        LOG_CRITICAL(HW_GPU, "Unimplemented program_type={}", static_cast<u32>(program_type));
        UNREACHABLE();
        return;
    }

    entries = program_result.second;
    shader_length = entries.shader_length;

    if (program_type != Maxwell::ShaderProgram::Geometry) {
        OGLShader shader;
        shader.Create(program_result.first.c_str(), gl_type);
        program.Create(true, shader.handle);
        SetShaderUniformBlockBindings(program.handle);
        LabelGLObject(GL_PROGRAM, program.handle, addr);
    } else {
        // Store shader's code to lazily build it on draw
        geometry_programs.code = program_result.first;
    }
}

GLuint CachedShader::GetProgramResourceIndex(const GLShader::ConstBufferEntry& buffer) {
    const auto search{resource_cache.find(buffer.GetHash())};
    if (search == resource_cache.end()) {
        const GLuint index{
            glGetProgramResourceIndex(program.handle, GL_UNIFORM_BLOCK, buffer.GetName().c_str())};
        resource_cache[buffer.GetHash()] = index;
        return index;
    }

    return search->second;
}

GLint CachedShader::GetUniformLocation(const GLShader::SamplerEntry& sampler) {
    const auto search{uniform_cache.find(sampler.GetHash())};
    if (search == uniform_cache.end()) {
        const GLint index{glGetUniformLocation(program.handle, sampler.GetName().c_str())};
        uniform_cache[sampler.GetHash()] = index;
        return index;
    }

    return search->second;
}

GLuint CachedShader::LazyGeometryProgram(OGLProgram& target_program,
                                         const std::string& glsl_topology, u32 max_vertices,
                                         const std::string& debug_name) {
    if (target_program.handle != 0) {
        return target_program.handle;
    }
    std::string source = "#version 430 core\n";
    source += "layout (" + glsl_topology + ") in;\n";
    source += "#define MAX_VERTEX_INPUT " + std::to_string(max_vertices) + '\n';
    source += geometry_programs.code;

    OGLShader shader;
    shader.Create(source.c_str(), GL_GEOMETRY_SHADER);
    target_program.Create(true, shader.handle);
    SetShaderUniformBlockBindings(target_program.handle);
    LabelGLObject(GL_PROGRAM, target_program.handle, addr, debug_name);
    return target_program.handle;
};

ShaderCacheOpenGL::ShaderCacheOpenGL(RasterizerOpenGL& rasterizer) : RasterizerCache{rasterizer} {}

Shader ShaderCacheOpenGL::GetStageProgram(Maxwell::ShaderProgram program) {
    const VAddr program_addr{GetShaderAddress(program)};

    // Look up shader in the cache based on address
    Shader shader{TryGet(program_addr)};

    if (!shader) {
        // No shader found - create a new one
        shader = std::make_shared<CachedShader>(program_addr, program);
        Register(shader);
    }

    return shader;
}

} // namespace OpenGL
