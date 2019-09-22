// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <mutex>
#include <thread>
#include <boost/functional/hash.hpp>
#include "common/assert.h"
#include "common/hash.h"
#include "common/scope_exit.h"
#include "core/core.h"
#include "core/frontend/emu_window.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/memory_manager.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/renderer_opengl/gl_shader_cache.h"
#include "video_core/renderer_opengl/gl_shader_decompiler.h"
#include "video_core/renderer_opengl/gl_shader_disk_cache.h"
#include "video_core/renderer_opengl/utils.h"
#include "video_core/shader/shader_ir.h"

namespace OpenGL {

using VideoCommon::Shader::ProgramCode;

// One UBO is always reserved for emulation values on staged shaders
constexpr u32 STAGE_RESERVED_UBOS = 1;

struct UnspecializedShader {
    std::string code;
    GLShader::ShaderEntries entries;
    ProgramType program_type;
};

namespace {

/// Gets the address for the specified shader stage program
GPUVAddr GetShaderAddress(Core::System& system, Maxwell::ShaderProgram program) {
    const auto& gpu{system.GPU().Maxwell3D()};
    const auto& shader_config{gpu.regs.shader_config[static_cast<std::size_t>(program)]};
    return gpu.regs.code_address.CodeAddress() + shader_config.offset;
}

/// Gets the shader program code from memory for the specified address
ProgramCode GetShaderCode(Tegra::MemoryManager& memory_manager, const GPUVAddr gpu_addr,
                          const u8* host_ptr) {
    ProgramCode program_code(VideoCommon::Shader::MAX_PROGRAM_LENGTH);
    ASSERT_OR_EXECUTE(host_ptr != nullptr, {
        std::fill(program_code.begin(), program_code.end(), 0);
        return program_code;
    });
    memory_manager.ReadBlockUnsafe(gpu_addr, program_code.data(),
                                   program_code.size() * sizeof(u64));
    return program_code;
}

/// Gets the shader type from a Maxwell program type
constexpr GLenum GetShaderType(ProgramType program_type) {
    switch (program_type) {
    case ProgramType::VertexA:
    case ProgramType::VertexB:
        return GL_VERTEX_SHADER;
    case ProgramType::Geometry:
        return GL_GEOMETRY_SHADER;
    case ProgramType::Fragment:
        return GL_FRAGMENT_SHADER;
    case ProgramType::Compute:
        return GL_COMPUTE_SHADER;
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

ProgramType GetProgramType(Maxwell::ShaderProgram program) {
    switch (program) {
    case Maxwell::ShaderProgram::VertexA:
        return ProgramType::VertexA;
    case Maxwell::ShaderProgram::VertexB:
        return ProgramType::VertexB;
    case Maxwell::ShaderProgram::TesselationControl:
        return ProgramType::TessellationControl;
    case Maxwell::ShaderProgram::TesselationEval:
        return ProgramType::TessellationEval;
    case Maxwell::ShaderProgram::Geometry:
        return ProgramType::Geometry;
    case Maxwell::ShaderProgram::Fragment:
        return ProgramType::Fragment;
    }
    UNREACHABLE();
    return {};
}

/// Calculates the size of a program stream
std::size_t CalculateProgramSize(const GLShader::ProgramCode& program) {
    constexpr std::size_t start_offset = 10;
    // This is the encoded version of BRA that jumps to itself. All Nvidia
    // shaders end with one.
    constexpr u64 self_jumping_branch = 0xE2400FFFFF07000FULL;
    constexpr u64 mask = 0xFFFFFFFFFF7FFFFFULL;
    std::size_t offset = start_offset;
    std::size_t size = start_offset * sizeof(u64);
    while (offset < program.size()) {
        const u64 instruction = program[offset];
        if (!IsSchedInstruction(offset, start_offset)) {
            if ((instruction & mask) == self_jumping_branch) {
                // End on Maxwell's "nop" instruction
                break;
            }
            if (instruction == 0) {
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
u64 GetUniqueIdentifier(ProgramType program_type, const ProgramCode& code,
                        const ProgramCode& code_b, std::size_t size_a = 0, std::size_t size_b = 0) {
    if (size_a == 0) {
        size_a = CalculateProgramSize(code);
    }
    u64 unique_identifier = Common::CityHash64(reinterpret_cast<const char*>(code.data()), size_a);
    if (program_type != ProgramType::VertexA) {
        return unique_identifier;
    }
    // VertexA programs include two programs

    std::size_t seed = 0;
    boost::hash_combine(seed, unique_identifier);

    if (size_b == 0) {
        size_b = CalculateProgramSize(code_b);
    }
    const u64 identifier_b =
        Common::CityHash64(reinterpret_cast<const char*>(code_b.data()), size_b);
    boost::hash_combine(seed, identifier_b);
    return static_cast<u64>(seed);
}

/// Creates an unspecialized program from code streams
GLShader::ProgramResult CreateProgram(const Device& device, ProgramType program_type,
                                      ProgramCode program_code, ProgramCode program_code_b) {
    GLShader::ShaderSetup setup(program_code);
    setup.program.size_a = CalculateProgramSize(program_code);
    setup.program.size_b = 0;
    if (program_type == ProgramType::VertexA) {
        // VertexB is always enabled, so when VertexA is enabled, we have two vertex shaders.
        // Conventional HW does not support this, so we combine VertexA and VertexB into one
        // stage here.
        setup.SetProgramB(program_code_b);
        setup.program.size_b = CalculateProgramSize(program_code_b);
    }
    setup.program.unique_identifier = GetUniqueIdentifier(
        program_type, program_code, program_code_b, setup.program.size_a, setup.program.size_b);

    switch (program_type) {
    case ProgramType::VertexA:
    case ProgramType::VertexB:
        return GLShader::GenerateVertexShader(device, setup);
    case ProgramType::Geometry:
        return GLShader::GenerateGeometryShader(device, setup);
    case ProgramType::Fragment:
        return GLShader::GenerateFragmentShader(device, setup);
    case ProgramType::Compute:
        return GLShader::GenerateComputeShader(device, setup);
    default:
        UNIMPLEMENTED_MSG("Unimplemented program_type={}", static_cast<u32>(program_type));
        return {};
    }
}

CachedProgram SpecializeShader(const std::string& code, const GLShader::ShaderEntries& entries,
                               ProgramType program_type, const ProgramVariant& variant,
                               bool hint_retrievable = false) {
    auto base_bindings{variant.base_bindings};
    const auto primitive_mode{variant.primitive_mode};
    const auto texture_buffer_usage{variant.texture_buffer_usage};

    std::string source = R"(#version 430 core
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shader_viewport_layer_array : enable
#extension GL_EXT_shader_image_load_formatted : enable
#extension GL_NV_gpu_shader5 : enable
#extension GL_NV_shader_thread_group : enable
#extension GL_NV_shader_thread_shuffle : enable
)";
    if (program_type == ProgramType::Compute) {
        source += "#extension GL_ARB_compute_variable_group_size : require\n";
    }
    source += '\n';

    if (program_type != ProgramType::Compute) {
        source += fmt::format("#define EMULATION_UBO_BINDING {}\n", base_bindings.cbuf++);
    }

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
    for (const auto& image : entries.images) {
        source +=
            fmt::format("#define IMAGE_BINDING_{} {}\n", image.GetIndex(), base_bindings.image++);
    }

    // Transform 1D textures to texture samplers by declaring its preprocessor macros.
    for (std::size_t i = 0; i < texture_buffer_usage.size(); ++i) {
        if (!texture_buffer_usage.test(i)) {
            continue;
        }
        source += fmt::format("#define SAMPLER_{}_IS_BUFFER\n", i);
    }
    if (texture_buffer_usage.any()) {
        source += '\n';
    }

    if (program_type == ProgramType::Geometry) {
        const auto [glsl_topology, debug_name, max_vertices] =
            GetPrimitiveDescription(primitive_mode);

        source += "layout (" + std::string(glsl_topology) + ") in;\n\n";
        source += "#define MAX_VERTEX_INPUT " + std::to_string(max_vertices) + '\n';
    }
    if (program_type == ProgramType::Compute) {
        source += "layout (local_size_variable) in;\n";
    }

    source += '\n';
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

} // Anonymous namespace

CachedShader::CachedShader(const ShaderParameters& params, ProgramType program_type,
                           GLShader::ProgramResult result)
    : RasterizerCacheObject{params.host_ptr}, cpu_addr{params.cpu_addr},
      unique_identifier{params.unique_identifier}, program_type{program_type},
      disk_cache{params.disk_cache}, precompiled_programs{params.precompiled_programs},
      entries{result.second}, code{std::move(result.first)}, shader_length{entries.shader_length} {}

Shader CachedShader::CreateStageFromMemory(const ShaderParameters& params,
                                           Maxwell::ShaderProgram program_type,
                                           ProgramCode&& program_code,
                                           ProgramCode&& program_code_b) {
    const auto code_size{CalculateProgramSize(program_code)};
    const auto code_size_b{CalculateProgramSize(program_code_b)};
    auto result{
        CreateProgram(params.device, GetProgramType(program_type), program_code, program_code_b)};
    if (result.first.empty()) {
        // TODO(Rodrigo): Unimplemented shader stages hit here, avoid using these for now
        return {};
    }

    params.disk_cache.SaveRaw(ShaderDiskCacheRaw(
        params.unique_identifier, GetProgramType(program_type),
        static_cast<u32>(code_size / sizeof(u64)), static_cast<u32>(code_size_b / sizeof(u64)),
        std::move(program_code), std::move(program_code_b)));

    return std::shared_ptr<CachedShader>(
        new CachedShader(params, GetProgramType(program_type), std::move(result)));
}

Shader CachedShader::CreateStageFromCache(const ShaderParameters& params,
                                          Maxwell::ShaderProgram program_type,
                                          GLShader::ProgramResult result) {
    return std::shared_ptr<CachedShader>(
        new CachedShader(params, GetProgramType(program_type), std::move(result)));
}

Shader CachedShader::CreateKernelFromMemory(const ShaderParameters& params, ProgramCode&& code) {
    auto result{CreateProgram(params.device, ProgramType::Compute, code, {})};

    const auto code_size{CalculateProgramSize(code)};
    params.disk_cache.SaveRaw(ShaderDiskCacheRaw(params.unique_identifier, ProgramType::Compute,
                                                 static_cast<u32>(code_size / sizeof(u64)), 0,
                                                 std::move(code), {}));

    return std::shared_ptr<CachedShader>(
        new CachedShader(params, ProgramType::Compute, std::move(result)));
}

Shader CachedShader::CreateKernelFromCache(const ShaderParameters& params,
                                           GLShader::ProgramResult result) {
    return std::shared_ptr<CachedShader>(
        new CachedShader(params, ProgramType::Compute, std::move(result)));
}

std::tuple<GLuint, BaseBindings> CachedShader::GetProgramHandle(const ProgramVariant& variant) {
    const auto [entry, is_cache_miss] = programs.try_emplace(variant);
    auto& program = entry->second;
    if (is_cache_miss) {
        program = TryLoadProgram(variant);
        if (!program) {
            program = SpecializeShader(code, entries, program_type, variant);
            disk_cache.SaveUsage(GetUsage(variant));
        }

        LabelGLObject(GL_PROGRAM, program->handle, cpu_addr);
    }

    auto base_bindings = variant.base_bindings;
    base_bindings.cbuf += static_cast<u32>(entries.const_buffers.size());
    if (program_type != ProgramType::Compute) {
        base_bindings.cbuf += STAGE_RESERVED_UBOS;
    }
    base_bindings.gmem += static_cast<u32>(entries.global_memory_entries.size());
    base_bindings.sampler += static_cast<u32>(entries.samplers.size());

    return {program->handle, base_bindings};
}

CachedProgram CachedShader::TryLoadProgram(const ProgramVariant& variant) const {
    const auto found = precompiled_programs.find(GetUsage(variant));
    if (found == precompiled_programs.end()) {
        return {};
    }
    return found->second;
}

ShaderDiskCacheUsage CachedShader::GetUsage(const ProgramVariant& variant) const {
    ShaderDiskCacheUsage usage;
    usage.unique_identifier = unique_identifier;
    usage.variant = variant;
    return usage;
}

ShaderCacheOpenGL::ShaderCacheOpenGL(RasterizerOpenGL& rasterizer, Core::System& system,
                                     Core::Frontend::EmuWindow& emu_window, const Device& device)
    : RasterizerCache{rasterizer}, system{system}, emu_window{emu_window}, device{device},
      disk_cache{system} {}

void ShaderCacheOpenGL::LoadDiskCache(const std::atomic_bool& stop_loading,
                                      const VideoCore::DiskResourceLoadCallback& callback) {
    const auto transferable = disk_cache.LoadTransferable();
    if (!transferable) {
        return;
    }
    const auto [raws, shader_usages] = *transferable;

    auto [decompiled, dumps] = disk_cache.LoadPrecompiled();

    const auto supported_formats{GetSupportedFormats()};
    const auto unspecialized_shaders{
        GenerateUnspecializedShaders(stop_loading, callback, raws, decompiled)};
    if (stop_loading) {
        return;
    }

    // Track if precompiled cache was altered during loading to know if we have to serialize the
    // virtual precompiled cache file back to the hard drive
    bool precompiled_cache_altered = false;

    // Inform the frontend about shader build initialization
    if (callback) {
        callback(VideoCore::LoadCallbackStage::Build, 0, shader_usages.size());
    }

    std::mutex mutex;
    std::size_t built_shaders = 0; // It doesn't have be atomic since it's used behind a mutex
    std::atomic_bool compilation_failed = false;

    const auto Worker = [&](Core::Frontend::GraphicsContext* context, std::size_t begin,
                            std::size_t end, const std::vector<ShaderDiskCacheUsage>& shader_usages,
                            const ShaderDumpsMap& dumps) {
        context->MakeCurrent();
        SCOPE_EXIT({ return context->DoneCurrent(); });

        for (std::size_t i = begin; i < end; ++i) {
            if (stop_loading || compilation_failed) {
                return;
            }
            const auto& usage{shader_usages[i]};
            LOG_INFO(Render_OpenGL, "Building shader {:016x} (index {} of {})",
                     usage.unique_identifier, i, shader_usages.size());

            const auto& unspecialized{unspecialized_shaders.at(usage.unique_identifier)};
            const auto dump{dumps.find(usage)};

            CachedProgram shader;
            if (dump != dumps.end()) {
                // If the shader is dumped, attempt to load it with
                shader = GeneratePrecompiledProgram(dump->second, supported_formats);
                if (!shader) {
                    compilation_failed = true;
                    return;
                }
            }
            if (!shader) {
                shader = SpecializeShader(unspecialized.code, unspecialized.entries,
                                          unspecialized.program_type, usage.variant, true);
            }

            std::scoped_lock lock(mutex);
            if (callback) {
                callback(VideoCore::LoadCallbackStage::Build, ++built_shaders,
                         shader_usages.size());
            }

            precompiled_programs.emplace(usage, std::move(shader));
        }
    };

    const auto num_workers{static_cast<std::size_t>(std::thread::hardware_concurrency() + 1)};
    const std::size_t bucket_size{shader_usages.size() / num_workers};
    std::vector<std::unique_ptr<Core::Frontend::GraphicsContext>> contexts(num_workers);
    std::vector<std::thread> threads(num_workers);
    for (std::size_t i = 0; i < num_workers; ++i) {
        const bool is_last_worker = i + 1 == num_workers;
        const std::size_t start{bucket_size * i};
        const std::size_t end{is_last_worker ? shader_usages.size() : start + bucket_size};

        // On some platforms the shared context has to be created from the GUI thread
        contexts[i] = emu_window.CreateSharedContext();
        threads[i] = std::thread(Worker, contexts[i].get(), start, end, shader_usages, dumps);
    }
    for (auto& thread : threads) {
        thread.join();
    }

    if (compilation_failed) {
        // Invalidate the precompiled cache if a shader dumped shader was rejected
        disk_cache.InvalidatePrecompiled();
        dumps.clear();
        precompiled_cache_altered = true;
        return;
    }
    if (stop_loading) {
        return;
    }

    // TODO(Rodrigo): Do state tracking for transferable shaders and do a dummy draw before
    // precompiling them

    for (std::size_t i = 0; i < shader_usages.size(); ++i) {
        const auto& usage{shader_usages[i]};
        if (dumps.find(usage) == dumps.end()) {
            const auto& program{precompiled_programs.at(usage)};
            disk_cache.SaveDump(usage, program->handle);
            precompiled_cache_altered = true;
        }
    }

    if (precompiled_cache_altered) {
        disk_cache.SaveVirtualPrecompiledFile();
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

    if (callback) {
        callback(VideoCore::LoadCallbackStage::Decompile, 0, raws.size());
    }

    for (std::size_t i = 0; i < raws.size(); ++i) {
        if (stop_loading) {
            return {};
        }
        const auto& raw{raws[i]};
        const u64 unique_identifier{raw.GetUniqueIdentifier()};
        const u64 calculated_hash{
            GetUniqueIdentifier(raw.GetProgramType(), raw.GetProgramCode(), raw.GetProgramCodeB())};
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
            result = CreateProgram(device, raw.GetProgramType(), raw.GetProgramCode(),
                                   raw.GetProgramCodeB());
            disk_cache.SaveDecompiled(unique_identifier, result.first, result.second);
        }

        precompiled_shaders.insert({unique_identifier, result});

        unspecialized.insert(
            {raw.GetUniqueIdentifier(),
             {std::move(result.first), std::move(result.second), raw.GetProgramType()}});

        if (callback) {
            callback(VideoCore::LoadCallbackStage::Decompile, i, raws.size());
        }
    }
    return unspecialized;
}

Shader ShaderCacheOpenGL::GetStageProgram(Maxwell::ShaderProgram program) {
    if (!system.GPU().Maxwell3D().dirty.shaders) {
        return last_shaders[static_cast<std::size_t>(program)];
    }

    auto& memory_manager{system.GPU().MemoryManager()};
    const GPUVAddr program_addr{GetShaderAddress(system, program)};

    // Look up shader in the cache based on address
    const auto host_ptr{memory_manager.GetPointer(program_addr)};
    Shader shader{TryGet(host_ptr)};
    if (shader) {
        return last_shaders[static_cast<std::size_t>(program)] = shader;
    }

    // No shader found - create a new one
    ProgramCode program_code{GetShaderCode(memory_manager, program_addr, host_ptr)};
    ProgramCode program_code_b;
    const bool is_program_a{program == Maxwell::ShaderProgram::VertexA};
    if (is_program_a) {
        const GPUVAddr program_addr_b{GetShaderAddress(system, Maxwell::ShaderProgram::VertexB)};
        program_code_b = GetShaderCode(memory_manager, program_addr_b,
                                       memory_manager.GetPointer(program_addr_b));
    }

    const auto unique_identifier =
        GetUniqueIdentifier(GetProgramType(program), program_code, program_code_b);
    const auto cpu_addr{*memory_manager.GpuToCpuAddress(program_addr)};
    const ShaderParameters params{disk_cache, precompiled_programs, device, cpu_addr,
                                  host_ptr,   unique_identifier};

    const auto found = precompiled_shaders.find(unique_identifier);
    if (found == precompiled_shaders.end()) {
        shader = CachedShader::CreateStageFromMemory(params, program, std::move(program_code),
                                                     std::move(program_code_b));
    } else {
        shader = CachedShader::CreateStageFromCache(params, program, found->second);
    }
    Register(shader);

    return last_shaders[static_cast<std::size_t>(program)] = shader;
}

Shader ShaderCacheOpenGL::GetComputeKernel(GPUVAddr code_addr) {
    auto& memory_manager{system.GPU().MemoryManager()};
    const auto host_ptr{memory_manager.GetPointer(code_addr)};
    auto kernel = TryGet(host_ptr);
    if (kernel) {
        return kernel;
    }

    // No kernel found - create a new one
    auto code{GetShaderCode(memory_manager, code_addr, host_ptr)};
    const auto unique_identifier{GetUniqueIdentifier(ProgramType::Compute, code, {})};
    const auto cpu_addr{*memory_manager.GpuToCpuAddress(code_addr)};
    const ShaderParameters params{disk_cache, precompiled_programs, device, cpu_addr,
                                  host_ptr,   unique_identifier};

    const auto found = precompiled_shaders.find(unique_identifier);
    if (found == precompiled_shaders.end()) {
        kernel = CachedShader::CreateKernelFromMemory(params, std::move(code));
    } else {
        kernel = CachedShader::CreateKernelFromCache(params, found->second);
    }

    Register(kernel);
    return kernel;
}

} // namespace OpenGL
