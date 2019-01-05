// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <boost/functional/hash.hpp>
#include "common/assert.h"
#include "common/hash.h"
#include "core/core.h"
#include "core/memory.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/renderer_opengl/gl_shader_cache.h"
#include "video_core/renderer_opengl/gl_shader_decompiler.h"
#include "video_core/renderer_opengl/gl_shader_manager.h"
#include "video_core/renderer_opengl/utils.h"
#include "video_core/shader/shader_ir.h"

namespace OpenGL {

using VideoCommon::Shader::ProgramCode;

/// Gets the address for the specified shader stage program
static VAddr GetShaderAddress(Maxwell::ShaderProgram program) {
    const auto& gpu = Core::System::GetInstance().GPU().Maxwell3D();
    const auto& shader_config = gpu.regs.shader_config[static_cast<std::size_t>(program)];
    return *gpu.memory_manager.GpuToCpuAddress(gpu.regs.code_address.CodeAddress() +
                                               shader_config.offset);
}

/// Gets the shader program code from memory for the specified address
static ProgramCode GetShaderCode(VAddr addr) {
    ProgramCode program_code(VideoCommon::Shader::MAX_PROGRAM_LENGTH);
    Memory::ReadBlock(addr, program_code.data(), program_code.size() * sizeof(u64));
    return program_code;
}

/// Gets the shader type from a Maxwell program type
constexpr GLenum GetShaderType(Maxwell::ShaderProgram program_type) {
    switch (program_type) {
    case Maxwell::ShaderProgram::VertexA:
    case Maxwell::ShaderProgram::VertexB:
        return GL_VERTEX_SHADER;
    case Maxwell::ShaderProgram::Geometry:
        return GL_GEOMETRY_SHADER;
    case Maxwell::ShaderProgram::Fragment:
        return GL_FRAGMENT_SHADER;
    default:
        return GL_NONE;
    }
}

CachedShader::CachedShader(VAddr addr, Maxwell::ShaderProgram program_type)
    : addr{addr}, program_type{program_type}, setup{GetShaderCode(addr)} {

    GLShader::ProgramResult program_result;

    switch (program_type) {
    case Maxwell::ShaderProgram::VertexA:
        // VertexB is always enabled, so when VertexA is enabled, we have two vertex shaders.
        // Conventional HW does not support this, so we combine VertexA and VertexB into one
        // stage here.
        setup.SetProgramB(GetShaderCode(GetShaderAddress(Maxwell::ShaderProgram::VertexB)));
    case Maxwell::ShaderProgram::VertexB:
        CalculateProperties();
        program_result = GLShader::GenerateVertexShader(setup);
        break;
    case Maxwell::ShaderProgram::Geometry:
        CalculateProperties();
        program_result = GLShader::GenerateGeometryShader(setup);
        break;
    case Maxwell::ShaderProgram::Fragment:
        CalculateProperties();
        program_result = GLShader::GenerateFragmentShader(setup);
        break;
    default:
        LOG_CRITICAL(HW_GPU, "Unimplemented program_type={}", static_cast<u32>(program_type));
        UNREACHABLE();
        return;
    }

    code = program_result.first;
    entries = program_result.second;
    shader_length = entries.shader_length;
}

std::tuple<GLuint, BaseBindings> CachedShader::GetProgramHandle(GLenum primitive_mode,
                                                                BaseBindings base_bindings) {
    GLuint handle{};
    if (program_type == Maxwell::ShaderProgram::Geometry) {
        handle = GetGeometryShader(primitive_mode, base_bindings);
    } else {
        const auto [entry, is_cache_miss] = programs.try_emplace(base_bindings);
        auto& program = entry->second;
        if (is_cache_miss) {
            std::string source = AllocateBindings(base_bindings);
            source += code;

            OGLShader shader;
            shader.Create(source.c_str(), GetShaderType(program_type));
            program.Create(true, shader.handle);
            LabelGLObject(GL_PROGRAM, program.handle, addr);
        }

        handle = program.handle;
    }

    // Add const buffer and samplers offset reserved by this shader. One UBO binding is reserved for
    // emulation values
    base_bindings.cbuf += static_cast<u32>(entries.const_buffers.size()) + 1;
    base_bindings.gmem += static_cast<u32>(entries.global_memory_entries.size());
    base_bindings.sampler += static_cast<u32>(entries.samplers.size());

    return {handle, base_bindings};
}

std::string CachedShader::AllocateBindings(BaseBindings base_bindings) {
    std::string code = "#version 430 core\n";
    code += fmt::format("#define EMULATION_UBO_BINDING {}\n", base_bindings.cbuf++);

    for (const auto& cbuf : entries.const_buffers) {
        code += fmt::format("#define CBUF_BINDING_{} {}\n", cbuf.GetIndex(), base_bindings.cbuf++);
    }

    for (const auto& gmem : entries.global_memory_entries) {
        code += fmt::format("#define GMEM_BINDING_{}_{} {}\n", gmem.GetCbufIndex(),
                            gmem.GetCbufOffset(), base_bindings.gmem++);
    }

    for (const auto& sampler : entries.samplers) {
        code += fmt::format("#define SAMPLER_BINDING_{} {}\n", sampler.GetIndex(),
                            base_bindings.sampler++);
    }

    return code;
}

GLuint CachedShader::GetGeometryShader(GLenum primitive_mode, BaseBindings base_bindings) {
    const auto [entry, is_cache_miss] = geometry_programs.try_emplace(base_bindings);
    auto& programs = entry->second;

    switch (primitive_mode) {
    case GL_POINTS:
        return LazyGeometryProgram(programs.points, base_bindings, "points", 1, "ShaderPoints");
    case GL_LINES:
    case GL_LINE_STRIP:
        return LazyGeometryProgram(programs.lines, base_bindings, "lines", 2, "ShaderLines");
    case GL_LINES_ADJACENCY:
    case GL_LINE_STRIP_ADJACENCY:
        return LazyGeometryProgram(programs.lines_adjacency, base_bindings, "lines_adjacency", 4,
                                   "ShaderLinesAdjacency");
    case GL_TRIANGLES:
    case GL_TRIANGLE_STRIP:
    case GL_TRIANGLE_FAN:
        return LazyGeometryProgram(programs.triangles, base_bindings, "triangles", 3,
                                   "ShaderTriangles");
    case GL_TRIANGLES_ADJACENCY:
    case GL_TRIANGLE_STRIP_ADJACENCY:
        return LazyGeometryProgram(programs.triangles_adjacency, base_bindings,
                                   "triangles_adjacency", 6, "ShaderTrianglesAdjacency");
    default:
        UNREACHABLE_MSG("Unknown primitive mode.");
        return LazyGeometryProgram(programs.points, base_bindings, "points", 1, "ShaderPoints");
    }
}

GLuint CachedShader::LazyGeometryProgram(OGLProgram& target_program, BaseBindings base_bindings,
                                         const std::string& glsl_topology, u32 max_vertices,
                                         const std::string& debug_name) {
    if (target_program.handle != 0) {
        return target_program.handle;
    }
    std::string source = AllocateBindings(base_bindings);
    source += "layout (" + glsl_topology + ") in;\n";
    source += "#define MAX_VERTEX_INPUT " + std::to_string(max_vertices) + '\n';
    source += code;

    OGLShader shader;
    shader.Create(source.c_str(), GL_GEOMETRY_SHADER);
    target_program.Create(true, shader.handle);
    LabelGLObject(GL_PROGRAM, target_program.handle, addr, debug_name);
    return target_program.handle;
};

static bool IsSchedInstruction(std::size_t offset, std::size_t main_offset) {
    // sched instructions appear once every 4 instructions.
    static constexpr std::size_t SchedPeriod = 4;
    const std::size_t absolute_offset = offset - main_offset;
    return (absolute_offset % SchedPeriod) == 0;
}

static std::size_t CalculateProgramSize(const GLShader::ProgramCode& program) {
    constexpr std::size_t start_offset = 10;
    std::size_t offset = start_offset;
    std::size_t size = start_offset * sizeof(u64);
    while (offset < program.size()) {
        const u64 inst = program[offset];
        if (!IsSchedInstruction(offset, start_offset)) {
            if (inst == 0 || (inst >> 52) == 0x50b) {
                break;
            }
        }
        size += sizeof(inst);
        offset++;
    }
    return size;
}

void CachedShader::CalculateProperties() {
    setup.program.real_size = CalculateProgramSize(setup.program.code);
    setup.program.real_size_b = 0;
    setup.program.unique_identifier = Common::CityHash64(
        reinterpret_cast<const char*>(setup.program.code.data()), setup.program.real_size);
    if (program_type == Maxwell::ShaderProgram::VertexA) {
        std::size_t seed = 0;
        boost::hash_combine(seed, setup.program.unique_identifier);
        setup.program.real_size_b = CalculateProgramSize(setup.program.code_b);
        const u64 identifier_b = Common::CityHash64(
            reinterpret_cast<const char*>(setup.program.code_b.data()), setup.program.real_size_b);
        boost::hash_combine(seed, identifier_b);
        setup.program.unique_identifier = static_cast<u64>(seed);
    }
}

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
