// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <memory>
#include <set>
#include <tuple>
#include <unordered_map>

#include <glad/glad.h>

#include "common/assert.h"
#include "common/common_types.h"
#include "video_core/rasterizer_cache.h"
#include "video_core/renderer_base.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_shader_decompiler.h"
#include "video_core/renderer_opengl/gl_shader_disk_cache.h"
#include "video_core/renderer_opengl/gl_shader_gen.h"

namespace Core {
class System;
} // namespace Core

namespace OpenGL {

class CachedShader;
class RasterizerOpenGL;
struct UnspecializedShader;

using Shader = std::shared_ptr<CachedShader>;
using CachedProgram = std::shared_ptr<OGLProgram>;
using Maxwell = Tegra::Engines::Maxwell3D::Regs;
using PrecompiledPrograms = std::unordered_map<ShaderDiskCacheUsage, CachedProgram>;
using PrecompiledShaders = std::unordered_map<u64, GLShader::ProgramResult>;

class CachedShader final : public RasterizerCacheObject {
public:
    explicit CachedShader(VAddr cpu_addr, u64 unique_identifier,
                          Maxwell::ShaderProgram program_type, ShaderDiskCacheOpenGL& disk_cache,
                          const PrecompiledPrograms& precompiled_programs,
                          ProgramCode&& program_code, ProgramCode&& program_code_b, u8* host_ptr);

    explicit CachedShader(VAddr cpu_addr, u64 unique_identifier,
                          Maxwell::ShaderProgram program_type, ShaderDiskCacheOpenGL& disk_cache,
                          const PrecompiledPrograms& precompiled_programs,
                          GLShader::ProgramResult result, u8* host_ptr);

    VAddr GetCpuAddr() const override {
        return cpu_addr;
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
        CachedProgram points;
        CachedProgram lines;
        CachedProgram lines_adjacency;
        CachedProgram triangles;
        CachedProgram triangles_adjacency;
    };

    GLuint GetGeometryShader(GLenum primitive_mode, BaseBindings base_bindings);

    /// Generates a geometry shader or returns one that already exists.
    GLuint LazyGeometryProgram(CachedProgram& target_program, BaseBindings base_bindings,
                               GLenum primitive_mode);

    CachedProgram TryLoadProgram(GLenum primitive_mode, BaseBindings base_bindings) const;

    ShaderDiskCacheUsage GetUsage(GLenum primitive_mode, BaseBindings base_bindings) const;

    u8* host_ptr{};
    VAddr cpu_addr{};
    u64 unique_identifier{};
    Maxwell::ShaderProgram program_type{};
    ShaderDiskCacheOpenGL& disk_cache;
    const PrecompiledPrograms& precompiled_programs;

    std::size_t shader_length{};
    GLShader::ShaderEntries entries;

    std::string code;

    std::unordered_map<BaseBindings, CachedProgram> programs;
    std::unordered_map<BaseBindings, GeometryPrograms> geometry_programs;

    std::unordered_map<u32, GLuint> cbuf_resource_cache;
    std::unordered_map<u32, GLuint> gmem_resource_cache;
    std::unordered_map<u32, GLint> uniform_cache;
};

class ShaderCacheOpenGL final : public RasterizerCache<Shader> {
public:
    explicit ShaderCacheOpenGL(RasterizerOpenGL& rasterizer, Core::System& system);

    /// Loads disk cache for the current game
    void LoadDiskCache(const std::atomic_bool& stop_loading,
                       const VideoCore::DiskResourceLoadCallback& callback);

    /// Gets the current specified shader stage program
    Shader GetStageProgram(Maxwell::ShaderProgram program);

private:
    std::unordered_map<u64, UnspecializedShader> GenerateUnspecializedShaders(
        const std::atomic_bool& stop_loading, const VideoCore::DiskResourceLoadCallback& callback,
        const std::vector<ShaderDiskCacheRaw>& raws,
        const std::unordered_map<u64, ShaderDiskCacheDecompiled>& decompiled);

    CachedProgram GeneratePrecompiledProgram(const ShaderDiskCacheDump& dump,
                                             const std::set<GLenum>& supported_formats);

    std::array<Shader, Maxwell::MaxShaderProgram> last_shaders;

    ShaderDiskCacheOpenGL disk_cache;
    PrecompiledShaders precompiled_shaders;
    PrecompiledPrograms precompiled_programs;
};

} // namespace OpenGL
