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
#include "video_core/renderer_opengl/gl_shader_disk_cache.h"
#include "video_core/renderer_opengl/gl_shader_manager.h"
#include "video_core/renderer_opengl/utils.h"
#include "video_core/shader/shader_ir.h"

namespace OpenGL {

using VideoCommon::Shader::ProgramCode;

// One UBO is always reserved for emulation values
constexpr u32 RESERVED_UBOS = 1;

struct UnspecializedShader {
    std::string code;
    GLShader::ShaderEntries entries;
    Maxwell::ShaderProgram program_type;
};

namespace {

/// Gets the address for the specified shader stage program
GPUVAddr GetShaderAddress(Maxwell::ShaderProgram program) {
    const auto& gpu{Core::System::GetInstance().GPU().Maxwell3D()};
    const auto& shader_config{gpu.regs.shader_config[static_cast<std::size_t>(program)]};
    return gpu.regs.code_address.CodeAddress() + shader_config.offset;
}

/// Gets the shader program code from memory for the specified address
ProgramCode GetShaderCode(const u8* host_ptr) {
    ProgramCode program_code(VideoCommon::Shader::MAX_PROGRAM_LENGTH);
    std::memcpy(program_code.data(), host_ptr, program_code.size() * sizeof(u64));
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

/// Gets if the current instruction offset is a scheduler instruction
constexpr bool IsSchedInstruction(std::size_t offset, std::size_t main_offset) {
    // Sched instructions appear once every 4 instructions.
    constexpr std::size_t SchedPeriod = 4;
    const std::size_t absolute_offset = offset - main_offset;
    return (absolute_offset % SchedPeriod) == 0;
}

/// Describes primitive behavior on geometry shaders
constexpr std::tuple<const char*, const char*, u32> GetPrimitiveDescription(GLenum primitive_mode) {
    switch (primitive_mode) {
    case GL_POINTS:
        return {"points", "Points", 1};
    case GL_LINES:
    case GL_LINE_STRIP:
        return {"lines", "Lines", 2};
    case GL_LINES_ADJACENCY:
    case GL_LINE_STRIP_ADJACENCY:
        return {"lines_adjacency", "LinesAdj", 4};
    case GL_TRIANGLES:
    case GL_TRIANGLE_STRIP:
    case GL_TRIANGLE_FAN:
        return {"triangles", "Triangles", 3};
    case GL_TRIANGLES_ADJACENCY:
    case GL_TRIANGLE_STRIP_ADJACENCY:
        return {"triangles_adjacency", "TrianglesAdj", 6};
    default:
        return {"points", "Invalid", 1};
    }
}

/// Calculates the size of a program stream
std::size_t CalculateProgramSize(const GLShader::ProgramCode& program) {
    constexpr std::size_t start_offset = 10;
    std::size_t offset = start_offset;
    std::size_t size = start_offset * sizeof(u64);
    while (offset < program.size()) {
        const u64 instruction = program[offset];
        if (!IsSchedInstruction(offset, start_offset)) {
            if (instruction == 0 || (instruction >> 52) == 0x50b) {
                // End on Maxwell's "nop" instruction
                break;
            }
        }
        size += sizeof(u64);
        offset++;
    }
    // The last instruction is included in the program size
    return std::min(size + sizeof(u64), program.size() * sizeof(u64));
}

/// Hashes one (or two) program streams
u64 GetUniqueIdentifier(Maxwell::ShaderProgram program_type, const ProgramCode& code,
                        const ProgramCode& code_b) {
    u64 unique_identifier =
        Common::CityHash64(reinterpret_cast<const char*>(code.data()), CalculateProgramSize(code));
    if (program_type != Maxwell::ShaderProgram::VertexA) {
        return unique_identifier;
    }
    // VertexA programs include two programs

    std::size_t seed = 0;
    boost::hash_combine(seed, unique_identifier);

    const u64 identifier_b = Common::CityHash64(reinterpret_cast<const char*>(code_b.data()),
                                                CalculateProgramSize(code_b));
    boost::hash_combine(seed, identifier_b);
    return static_cast<u64>(seed);
}

/// Creates an unspecialized program from code streams
GLShader::ProgramResult CreateProgram(Maxwell::ShaderProgram program_type, ProgramCode program_code,
                                      ProgramCode program_code_b) {
    GLShader::ShaderSetup setup(program_code);
    if (program_type == Maxwell::ShaderProgram::VertexA) {
        // VertexB is always enabled, so when VertexA is enabled, we have two vertex shaders.
        // Conventional HW does not support this, so we combine VertexA and VertexB into one
        // stage here.
        setup.SetProgramB(program_code_b);
    }
    setup.program.unique_identifier =
        GetUniqueIdentifier(program_type, program_code, program_code_b);

    switch (program_type) {
    case Maxwell::ShaderProgram::VertexA:
    case Maxwell::ShaderProgram::VertexB:
        return GLShader::GenerateVertexShader(setup);
    case Maxwell::ShaderProgram::Geometry:
        return GLShader::GenerateGeometryShader(setup);
    case Maxwell::ShaderProgram::Fragment:
        return GLShader::GenerateFragmentShader(setup);
    default:
        LOG_CRITICAL(HW_GPU, "Unimplemented program_type={}", static_cast<u32>(program_type));
        UNREACHABLE();
        return {};
    }
}

CachedProgram SpecializeShader(const std::string& code, const GLShader::ShaderEntries& entries,
                               Maxwell::ShaderProgram program_type, BaseBindings base_bindings,
                               GLenum primitive_mode, bool hint_retrievable = false) {
    std::string source = "#version 430 core\n";
    source += fmt::format("#define EMULATION_UBO_BINDING {}\n", base_bindings.cbuf++);

    for (const auto& cbuf : entries.const_buffers) {
        source +=
            fmt::format("#define CBUF_BINDING_{} {}\n", cbuf.GetIndex(), base_bindings.cbuf++);
    }
    for (const auto& gmem : entries.global_memory_entries) {
        source += fmt::format("#define GMEM_BINDING_{}_{} {}\n", gmem.GetCbufIndex(),
                              gmem.GetCbufOffset(), base_bindings.gmem++);
    }
    for (const auto& sampler : entries.samplers) {
        source += fmt::format("#define SAMPLER_BINDING_{} {}\n", sampler.GetIndex(),
                              base_bindings.sampler++);
    }

    if (program_type == Maxwell::ShaderProgram::Geometry) {
        const auto [glsl_topology, debug_name, max_vertices] =
            GetPrimitiveDescription(primitive_mode);

        source += "layout (" + std::string(glsl_topology) + ") in;\n";
        source += "#define MAX_VERTEX_INPUT " + std::to_string(max_vertices) + '\n';
    }

    source += code;

    OGLShader shader;
    shader.Create(source.c_str(), GetShaderType(program_type));

    auto program = std::make_shared<OGLProgram>();
    program->Create(true, hint_retrievable, shader.handle);
    return program;
}

std::set<GLenum> GetSupportedFormats() {
    std::set<GLenum> supported_formats;

    GLint num_formats{};
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &num_formats);

    std::vector<GLint> formats(num_formats);
    glGetIntegerv(GL_PROGRAM_BINARY_FORMATS, formats.data());

    for (const GLint format : formats)
        supported_formats.insert(static_cast<GLenum>(format));
    return supported_formats;
}

} // namespace

CachedShader::CachedShader(VAddr cpu_addr, u64 unique_identifier,
                           Maxwell::ShaderProgram program_type, ShaderDiskCacheOpenGL& disk_cache,
                           const PrecompiledPrograms& precompiled_programs,
                           ProgramCode&& program_code, ProgramCode&& program_code_b, u8* host_ptr)
    : host_ptr{host_ptr}, cpu_addr{cpu_addr}, unique_identifier{unique_identifier},
      program_type{program_type}, disk_cache{disk_cache},
      precompiled_programs{precompiled_programs}, RasterizerCacheObject{host_ptr} {

    const std::size_t code_size = CalculateProgramSize(program_code);
    const std::size_t code_size_b =
        program_code_b.empty() ? 0 : CalculateProgramSize(program_code_b);

    GLShader::ProgramResult program_result =
        CreateProgram(program_type, program_code, program_code_b);
    if (program_result.first.empty()) {
        // TODO(Rodrigo): Unimplemented shader stages hit here, avoid using these for now
        return;
    }

    code = program_result.first;
    entries = program_result.second;
    shader_length = entries.shader_length;

    const ShaderDiskCacheRaw raw(unique_identifier, program_type,
                                 static_cast<u32>(code_size / sizeof(u64)),
                                 static_cast<u32>(code_size_b / sizeof(u64)),
                                 std::move(program_code), std::move(program_code_b));
    disk_cache.SaveRaw(raw);
}

CachedShader::CachedShader(VAddr cpu_addr, u64 unique_identifier,
                           Maxwell::ShaderProgram program_type, ShaderDiskCacheOpenGL& disk_cache,
                           const PrecompiledPrograms& precompiled_programs,
                           GLShader::ProgramResult result, u8* host_ptr)
    : cpu_addr{cpu_addr}, unique_identifier{unique_identifier}, program_type{program_type},
      disk_cache{disk_cache}, precompiled_programs{precompiled_programs}, RasterizerCacheObject{
                                                                              host_ptr} {

    code = std::move(result.first);
    entries = result.second;
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
            program = TryLoadProgram(primitive_mode, base_bindings);
            if (!program) {
                program =
                    SpecializeShader(code, entries, program_type, base_bindings, primitive_mode);
                disk_cache.SaveUsage(GetUsage(primitive_mode, base_bindings));
            }

            LabelGLObject(GL_PROGRAM, program->handle, cpu_addr);
        }

        handle = program->handle;
    }

    base_bindings.cbuf += static_cast<u32>(entries.const_buffers.size()) + RESERVED_UBOS;
    base_bindings.gmem += static_cast<u32>(entries.global_memory_entries.size());
    base_bindings.sampler += static_cast<u32>(entries.samplers.size());

    return {handle, base_bindings};
}

GLuint CachedShader::GetGeometryShader(GLenum primitive_mode, BaseBindings base_bindings) {
    const auto [entry, is_cache_miss] = geometry_programs.try_emplace(base_bindings);
    auto& programs = entry->second;

    switch (primitive_mode) {
    case GL_POINTS:
        return LazyGeometryProgram(programs.points, base_bindings, primitive_mode);
    case GL_LINES:
    case GL_LINE_STRIP:
        return LazyGeometryProgram(programs.lines, base_bindings, primitive_mode);
    case GL_LINES_ADJACENCY:
    case GL_LINE_STRIP_ADJACENCY:
        return LazyGeometryProgram(programs.lines_adjacency, base_bindings, primitive_mode);
    case GL_TRIANGLES:
    case GL_TRIANGLE_STRIP:
    case GL_TRIANGLE_FAN:
        return LazyGeometryProgram(programs.triangles, base_bindings, primitive_mode);
    case GL_TRIANGLES_ADJACENCY:
    case GL_TRIANGLE_STRIP_ADJACENCY:
        return LazyGeometryProgram(programs.triangles_adjacency, base_bindings, primitive_mode);
    default:
        UNREACHABLE_MSG("Unknown primitive mode.");
        return LazyGeometryProgram(programs.points, base_bindings, primitive_mode);
    }
}

GLuint CachedShader::LazyGeometryProgram(CachedProgram& target_program, BaseBindings base_bindings,
                                         GLenum primitive_mode) {
    if (target_program) {
        return target_program->handle;
    }
    const auto [glsl_name, debug_name, vertices] = GetPrimitiveDescription(primitive_mode);
    target_program = TryLoadProgram(primitive_mode, base_bindings);
    if (!target_program) {
        target_program =
            SpecializeShader(code, entries, program_type, base_bindings, primitive_mode);
        disk_cache.SaveUsage(GetUsage(primitive_mode, base_bindings));
    }

    LabelGLObject(GL_PROGRAM, target_program->handle, cpu_addr, debug_name);

    return target_program->handle;
};

CachedProgram CachedShader::TryLoadProgram(GLenum primitive_mode,
                                           BaseBindings base_bindings) const {
    const auto found = precompiled_programs.find(GetUsage(primitive_mode, base_bindings));
    if (found == precompiled_programs.end()) {
        return {};
    }
    return found->second;
}

ShaderDiskCacheUsage CachedShader::GetUsage(GLenum primitive_mode,
                                            BaseBindings base_bindings) const {
    return {unique_identifier, base_bindings, primitive_mode};
}

ShaderCacheOpenGL::ShaderCacheOpenGL(RasterizerOpenGL& rasterizer, Core::System& system)
    : RasterizerCache{rasterizer}, disk_cache{system} {}

void ShaderCacheOpenGL::LoadDiskCache(const std::atomic_bool& stop_loading,
                                      const VideoCore::DiskResourceLoadCallback& callback) {
    const auto transferable = disk_cache.LoadTransferable();
    if (!transferable) {
        return;
    }
    const auto [raws, usages] = *transferable;

    auto [decompiled, dumps] = disk_cache.LoadPrecompiled();

    const auto supported_formats{GetSupportedFormats()};
    const auto unspecialized{
        GenerateUnspecializedShaders(stop_loading, callback, raws, decompiled)};
    if (stop_loading)
        return;

    // Build shaders
    if (callback)
        callback(VideoCore::LoadCallbackStage::Build, 0, usages.size());
    for (std::size_t i = 0; i < usages.size(); ++i) {
        if (stop_loading)
            return;

        const auto& usage{usages[i]};
        LOG_INFO(Render_OpenGL, "Building shader {:016x} ({} of {})", usage.unique_identifier,
                 i + 1, usages.size());

        const auto& unspec{unspecialized.at(usage.unique_identifier)};
        const auto dump_it = dumps.find(usage);

        CachedProgram shader;
        if (dump_it != dumps.end()) {
            // If the shader is dumped, attempt to load it with
            shader = GeneratePrecompiledProgram(dump_it->second, supported_formats);
            if (!shader) {
                // Invalidate the precompiled cache if a shader dumped shader was rejected
                disk_cache.InvalidatePrecompiled();
                dumps.clear();
            }
        }
        if (!shader) {
            shader = SpecializeShader(unspec.code, unspec.entries, unspec.program_type,
                                      usage.bindings, usage.primitive, true);
        }
        precompiled_programs.insert({usage, std::move(shader)});

        if (callback)
            callback(VideoCore::LoadCallbackStage::Build, i + 1, usages.size());
    }

    // TODO(Rodrigo): Do state tracking for transferable shaders and do a dummy draw before
    // precompiling them

    for (std::size_t i = 0; i < usages.size(); ++i) {
        const auto& usage{usages[i]};
        if (dumps.find(usage) == dumps.end()) {
            const auto& program = precompiled_programs.at(usage);
            disk_cache.SaveDump(usage, program->handle);
        }
    }
}

CachedProgram ShaderCacheOpenGL::GeneratePrecompiledProgram(
    const ShaderDiskCacheDump& dump, const std::set<GLenum>& supported_formats) {

    if (supported_formats.find(dump.binary_format) == supported_formats.end()) {
        LOG_INFO(Render_OpenGL, "Precompiled cache entry with unsupported format - removing");
        return {};
    }

    CachedProgram shader = std::make_shared<OGLProgram>();
    shader->handle = glCreateProgram();
    glProgramParameteri(shader->handle, GL_PROGRAM_SEPARABLE, GL_TRUE);
    glProgramBinary(shader->handle, dump.binary_format, dump.binary.data(),
                    static_cast<GLsizei>(dump.binary.size()));

    GLint link_status{};
    glGetProgramiv(shader->handle, GL_LINK_STATUS, &link_status);
    if (link_status == GL_FALSE) {
        LOG_INFO(Render_OpenGL, "Precompiled cache rejected by the driver - removing");
        return {};
    }

    return shader;
}

std::unordered_map<u64, UnspecializedShader> ShaderCacheOpenGL::GenerateUnspecializedShaders(
    const std::atomic_bool& stop_loading, const VideoCore::DiskResourceLoadCallback& callback,
    const std::vector<ShaderDiskCacheRaw>& raws,
    const std::unordered_map<u64, ShaderDiskCacheDecompiled>& decompiled) {
    std::unordered_map<u64, UnspecializedShader> unspecialized;

    if (callback)
        callback(VideoCore::LoadCallbackStage::Decompile, 0, raws.size());

    for (std::size_t i = 0; i < raws.size(); ++i) {
        if (stop_loading)
            return {};

        const auto& raw{raws[i]};
        const u64 unique_identifier = raw.GetUniqueIdentifier();
        const u64 calculated_hash =
            GetUniqueIdentifier(raw.GetProgramType(), raw.GetProgramCode(), raw.GetProgramCodeB());
        if (unique_identifier != calculated_hash) {
            LOG_ERROR(
                Render_OpenGL,
                "Invalid hash in entry={:016x} (obtained hash={:016x}) - removing shader cache",
                raw.GetUniqueIdentifier(), calculated_hash);
            disk_cache.InvalidateTransferable();
            return {};
        }

        GLShader::ProgramResult result;
        if (const auto it = decompiled.find(unique_identifier); it != decompiled.end()) {
            // If it's stored in the precompiled file, avoid decompiling it here
            const auto& stored_decompiled{it->second};
            result = {stored_decompiled.code, stored_decompiled.entries};
        } else {
            // Otherwise decompile the shader at boot and save the result to the decompiled file
            result =
                CreateProgram(raw.GetProgramType(), raw.GetProgramCode(), raw.GetProgramCodeB());
            disk_cache.SaveDecompiled(unique_identifier, result.first, result.second);
        }

        precompiled_shaders.insert({unique_identifier, result});

        unspecialized.insert(
            {raw.GetUniqueIdentifier(),
             {std::move(result.first), std::move(result.second), raw.GetProgramType()}});

        if (callback)
            callback(VideoCore::LoadCallbackStage::Decompile, i, raws.size());
    }
    return unspecialized;
}

Shader ShaderCacheOpenGL::GetStageProgram(Maxwell::ShaderProgram program) {
    if (!Core::System::GetInstance().GPU().Maxwell3D().dirty_flags.shaders) {
        return last_shaders[static_cast<u32>(program)];
    }

    auto& memory_manager{Core::System::GetInstance().GPU().MemoryManager()};
    const GPUVAddr program_addr{GetShaderAddress(program)};

    // Look up shader in the cache based on address
    const auto& host_ptr{memory_manager.GetPointer(program_addr)};
    Shader shader{TryGet(host_ptr)};

    if (!shader) {
        // No shader found - create a new one
        ProgramCode program_code{GetShaderCode(host_ptr)};
        ProgramCode program_code_b;
        if (program == Maxwell::ShaderProgram::VertexA) {
            program_code_b = GetShaderCode(
                memory_manager.GetPointer(GetShaderAddress(Maxwell::ShaderProgram::VertexB)));
        }
        const u64 unique_identifier = GetUniqueIdentifier(program, program_code, program_code_b);
        const VAddr cpu_addr{*memory_manager.GpuToCpuAddress(program_addr)};
        const auto found = precompiled_shaders.find(unique_identifier);
        if (found != precompiled_shaders.end()) {
            shader =
                std::make_shared<CachedShader>(cpu_addr, unique_identifier, program, disk_cache,
                                               precompiled_programs, found->second, host_ptr);
        } else {
            shader = std::make_shared<CachedShader>(
                cpu_addr, unique_identifier, program, disk_cache, precompiled_programs,
                std::move(program_code), std::move(program_code_b), host_ptr);
        }
        Register(shader);
    }

    return last_shaders[static_cast<u32>(program)] = shader;
}

} // namespace OpenGL
