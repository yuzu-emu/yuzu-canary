// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "core/core.h"
#include "core/memory.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_opengl/gl_shader_cache.h"
#include "video_core/renderer_opengl/gl_shader_manager.h"

namespace OpenGL {

/// Gets the address for the specified shader stage program
static Tegra::GPUVAddr GetShaderAddress(Maxwell::ShaderProgram program) {
    auto& gpu = Core::System::GetInstance().GPU().Maxwell3D();

    GLShader::ProgramCode program_code(GLShader::MAX_PROGRAM_CODE_LENGTH);
    auto& shader_config = gpu.regs.shader_config[static_cast<size_t>(program)];
    return gpu.regs.code_address.CodeAddress() + shader_config.offset;
}

/// Gets the shader program code from memory for the specified address
static GLShader::ProgramCode GetShaderCode(Tegra::GPUVAddr addr) {
    auto& gpu = Core::System::GetInstance().GPU().Maxwell3D();

    GLShader::ProgramCode program_code(GLShader::MAX_PROGRAM_CODE_LENGTH);
    const boost::optional<VAddr> cpu_address{gpu.memory_manager.GpuToCpuAddress(addr)};
    Memory::ReadBlock(*cpu_address, program_code.data(), program_code.size() * sizeof(u64));

    return program_code;
}

/// Helper function to set shader uniform block bindings for a single shader stage
static void SetShaderUniformBlockBinding(GLuint shader, const char* name,
                                         Maxwell::ShaderStage binding, size_t expected_size) {
    const GLuint ub_index = glGetUniformBlockIndex(shader, name);
    if (ub_index == GL_INVALID_INDEX) {
        return;
    }

    GLint ub_size = 0;
    glGetActiveUniformBlockiv(shader, ub_index, GL_UNIFORM_BLOCK_DATA_SIZE, &ub_size);
    ASSERT_MSG(static_cast<size_t>(ub_size) == expected_size,
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

CachedShader::CachedShader(Tegra::GPUVAddr addr, Maxwell::ShaderProgram program_type)
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

    OGLShader shader;
    shader.Create(program_result.first.c_str(), gl_type);
    program.Create(true, shader.handle);
    SetShaderUniformBlockBindings(program.handle);
}

GLuint CachedShader::GetProgramResourceIndex(const std::string& name) {
    auto search{resource_cache.find(name)};
    if (search == resource_cache.end()) {
        const GLuint index{
            glGetProgramResourceIndex(program.handle, GL_UNIFORM_BLOCK, name.c_str())};
        resource_cache[name] = index;
        return index;
    }

    return search->second;
}

GLint CachedShader::GetUniformLocation(const std::string& name) {
    auto search{uniform_cache.find(name)};
    if (search == uniform_cache.end()) {
        const GLint index{glGetUniformLocation(program.handle, name.c_str())};
        uniform_cache[name] = index;
        return index;
    }

    return search->second;
}

Shader ShaderCacheOpenGL::GetStageProgram(Maxwell::ShaderProgram program) {
    const Tegra::GPUVAddr program_addr{GetShaderAddress(program)};

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
