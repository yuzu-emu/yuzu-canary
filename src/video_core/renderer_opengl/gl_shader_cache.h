// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <atomic>
#include <bitset>
#include <memory>
#include <set>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <glad/glad.h>

#include "common/common_types.h"
#include "video_core/rasterizer_cache.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_shader_decompiler.h"
#include "video_core/renderer_opengl/gl_shader_disk_cache.h"

namespace Core {
class System;
}

namespace Core::Frontend {
class EmuWindow;
}

namespace OpenGL {

class CachedShader;
class Device;
class RasterizerOpenGL;
struct UnspecializedShader;

using Shader = std::shared_ptr<CachedShader>;
using CachedProgram = std::shared_ptr<OGLProgram>;
using Maxwell = Tegra::Engines::Maxwell3D::Regs;
using PrecompiledPrograms = std::unordered_map<ShaderDiskCacheUsage, CachedProgram>;
using PrecompiledShaders = std::unordered_map<u64, GLShader::ProgramResult>;

struct ShaderParameters {
    ShaderDiskCacheOpenGL& disk_cache;
    const PrecompiledPrograms& precompiled_programs;
    const Device& device;
    VAddr cpu_addr;
    u8* host_ptr;
    u64 unique_identifier;
};

class CachedShader final : public RasterizerCacheObject {
public:
    static Shader CreateStageFromMemory(const ShaderParameters& params,
                                        Maxwell::ShaderProgram program_type,
                                        ProgramCode&& program_code, ProgramCode&& program_code_b);

    static Shader CreateStageFromCache(const ShaderParameters& params,
                                       Maxwell::ShaderProgram program_type,
                                       GLShader::ProgramResult result);

    static Shader CreateKernelFromMemory(const ShaderParameters& params, ProgramCode&& code);

    static Shader CreateKernelFromCache(const ShaderParameters& params,
                                        GLShader::ProgramResult result);

    VAddr GetCpuAddr() const override {
        return cpu_addr;
    }

    std::size_t GetSizeInBytes() const override {
        return shader_length;
    }

    /// Gets the shader entries for the shader
    const GLShader::ShaderEntries& GetShaderEntries() const {
        return entries;
    }

    /// Gets the GL program handle for the shader
    std::tuple<GLuint, BaseBindings> GetProgramHandle(const ProgramVariant& variant);

private:
    explicit CachedShader(const ShaderParameters& params, ProgramType program_type,
                          GLShader::ProgramResult result);

    // Geometry programs. These are needed because GLSL needs an input topology but it's not
    // declared by the hardware. Workaround this issue by generating a different shader per input
    // topology class.
    struct GeometryPrograms {
        CachedProgram points;
        CachedProgram lines;
        CachedProgram lines_adjacency;
        CachedProgram triangles;
        CachedProgram triangles_adjacency;
    };

    GLuint GetGeometryShader(const ProgramVariant& variant);

    /// Generates a geometry shader or returns one that already exists.
    GLuint LazyGeometryProgram(CachedProgram& target_program, const ProgramVariant& variant);

    CachedProgram TryLoadProgram(const ProgramVariant& variant) const;

    ShaderDiskCacheUsage GetUsage(const ProgramVariant& variant) const;

    VAddr cpu_addr{};
    u64 unique_identifier{};
    ProgramType program_type{};
    ShaderDiskCacheOpenGL& disk_cache;
    const PrecompiledPrograms& precompiled_programs;

    GLShader::ShaderEntries entries;
    std::string code;
    std::size_t shader_length{};

    std::unordered_map<ProgramVariant, CachedProgram> programs;
    std::unordered_map<ProgramVariant, GeometryPrograms> geometry_programs;

    std::unordered_map<u32, GLuint> cbuf_resource_cache;
    std::unordered_map<u32, GLuint> gmem_resource_cache;
    std::unordered_map<u32, GLint> uniform_cache;
};

class ShaderCacheOpenGL final : public RasterizerCache<Shader> {
public:
    explicit ShaderCacheOpenGL(RasterizerOpenGL& rasterizer, Core::System& system,
                               Core::Frontend::EmuWindow& emu_window, const Device& device);

    /// Loads disk cache for the current game
    void LoadDiskCache(const std::atomic_bool& stop_loading,
                       const VideoCore::DiskResourceLoadCallback& callback);

    /// Gets the current specified shader stage program
    Shader GetStageProgram(Maxwell::ShaderProgram program);

    /// Gets a compute kernel in the passed address
    Shader GetComputeKernel(GPUVAddr code_addr);

protected:
    // We do not have to flush this cache as things in it are never modified by us.
    void FlushObjectInner(const Shader& object) override {}

private:
    std::unordered_map<u64, UnspecializedShader> GenerateUnspecializedShaders(
        const std::atomic_bool& stop_loading, const VideoCore::DiskResourceLoadCallback& callback,
        const std::vector<ShaderDiskCacheRaw>& raws,
        const std::unordered_map<u64, ShaderDiskCacheDecompiled>& decompiled);

    CachedProgram GeneratePrecompiledProgram(const ShaderDiskCacheDump& dump,
                                             const std::set<GLenum>& supported_formats);

    Core::System& system;
    Core::Frontend::EmuWindow& emu_window;
    const Device& device;
    ShaderDiskCacheOpenGL disk_cache;

    PrecompiledShaders precompiled_shaders;
    PrecompiledPrograms precompiled_programs;
    std::array<Shader, Maxwell::MaxShaderProgram> last_shaders;
};

} // namespace OpenGL
