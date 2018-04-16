// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <tuple>
#include <unordered_map>
#include <boost/functional/hash.hpp>
#include <glad/glad.h>
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_shader_gen.h"
#include "video_core/renderer_opengl/maxwell_to_gl.h"

namespace GLShader {

/// Number of OpenGL texture samplers that can be used in the fragment shader
static constexpr size_t NumTextureSamplers = 32;

using Tegra::Engines::Maxwell3D;

namespace Impl {
void SetShaderUniformBlockBindings(GLuint shader);
void SetShaderSamplerBindings(GLuint shader);
} // namespace Impl

/// Uniform structure for the Uniform Buffer Object, all vectors must be 16-byte aligned
// NOTE: Always keep a vec4 at the end. The GL spec is not clear wether the alignment at
//       the end of a uniform block is included in UNIFORM_BLOCK_DATA_SIZE or not.
//       Not following that rule will cause problems on some AMD drivers.
struct MaxwellUniformData {
    void SetFromRegs(const Maxwell3D::State::ShaderStageInfo& shader_stage);
    alignas(16) GLvec4 viewport_flip;
};
static_assert(sizeof(MaxwellUniformData) == 16, "MaxwellUniformData structure size is incorrect");
static_assert(sizeof(MaxwellUniformData) < 16384,
              "MaxwellUniformData structure must be less than 16kb as per the OpenGL spec");

class OGLShaderStage {
public:
    OGLShaderStage() = default;

    void Create(const ProgramResult& program_result, GLenum type) {
        OGLShader shader;
        shader.Create(program_result.first.c_str(), type);
        program.Create(true, shader.handle);
        Impl::SetShaderUniformBlockBindings(program.handle);
        Impl::SetShaderSamplerBindings(program.handle);
        entries = program_result.second;
    }
    GLuint GetHandle() const {
        return program.handle;
    }

    ShaderEntries GetEntries() const {
        return entries;
    }

private:
    OGLProgram program;
    ShaderEntries entries;
};

// TODO(wwylele): beautify this doc
// This is a shader cache designed for translating PICA shader to GLSL shader.
// The double cache is needed because diffent KeyConfigType, which includes a hash of the code
// region (including its leftover unused code) can generate the same GLSL code.
template <typename KeyConfigType,
          ProgramResult (*CodeGenerator)(const ShaderSetup&, const KeyConfigType&),
          GLenum ShaderType>
class ShaderCache {
public:
    ShaderCache() = default;

    using Result = std::pair<GLuint, ShaderEntries>;

    Result Get(const KeyConfigType& key, const ShaderSetup& setup) {
        auto map_it = shader_map.find(key);
        if (map_it == shader_map.end()) {
            ProgramResult program = CodeGenerator(setup, key);

            auto [iter, new_shader] = shader_cache.emplace(program.first, OGLShaderStage{});
            OGLShaderStage& cached_shader = iter->second;
            if (new_shader) {
                cached_shader.Create(program, ShaderType);
            }
            shader_map[key] = &cached_shader;
            return {cached_shader.GetHandle(), program.second};
        } else {
            return {map_it->second->GetHandle(), map_it->second->GetEntries()};
        }
    }

private:
    std::unordered_map<KeyConfigType, OGLShaderStage*> shader_map;
    std::unordered_map<std::string, OGLShaderStage> shader_cache;
};

using VertexShaders = ShaderCache<MaxwellVSConfig, &GenerateVertexShader, GL_VERTEX_SHADER>;

using FragmentShaders = ShaderCache<MaxwellFSConfig, &GenerateFragmentShader, GL_FRAGMENT_SHADER>;

class ProgramManager {
public:
    ProgramManager() {
        pipeline.Create();
    }

    ShaderEntries UseProgrammableVertexShader(const MaxwellVSConfig& config,
                                              const ShaderSetup setup) {
        ShaderEntries result;
        std::tie(current.vs, result) = vertex_shaders.Get(config, setup);
        return result;
    }

    ShaderEntries UseProgrammableFragmentShader(const MaxwellFSConfig& config,
                                                const ShaderSetup setup) {
        ShaderEntries result;
        std::tie(current.fs, result) = fragment_shaders.Get(config, setup);
        return result;
    }

    GLuint GetCurrentProgramStage(Maxwell3D::Regs::ShaderStage stage) {
        switch (stage) {
        case Maxwell3D::Regs::ShaderStage::Vertex:
            return current.vs;
        case Maxwell3D::Regs::ShaderStage::Fragment:
            return current.fs;
        }

        UNREACHABLE();
    }

    void UseTrivialGeometryShader() {
        current.gs = 0;
    }

    void ApplyTo(OpenGLState& state) {
        // Workaround for AMD bug
        glUseProgramStages(pipeline.handle,
                           GL_VERTEX_SHADER_BIT | GL_GEOMETRY_SHADER_BIT | GL_FRAGMENT_SHADER_BIT,
                           0);

        glUseProgramStages(pipeline.handle, GL_VERTEX_SHADER_BIT, current.vs);
        glUseProgramStages(pipeline.handle, GL_GEOMETRY_SHADER_BIT, current.gs);
        glUseProgramStages(pipeline.handle, GL_FRAGMENT_SHADER_BIT, current.fs);
        state.draw.shader_program = 0;
        state.draw.program_pipeline = pipeline.handle;
    }

private:
    struct ShaderTuple {
        GLuint vs = 0, gs = 0, fs = 0;
        bool operator==(const ShaderTuple& rhs) const {
            return std::tie(vs, gs, fs) == std::tie(rhs.vs, rhs.gs, rhs.fs);
        }
        struct Hash {
            std::size_t operator()(const ShaderTuple& tuple) const {
                std::size_t hash = 0;
                boost::hash_combine(hash, tuple.vs);
                boost::hash_combine(hash, tuple.gs);
                boost::hash_combine(hash, tuple.fs);
                return hash;
            }
        };
    };
    ShaderTuple current;
    VertexShaders vertex_shaders;
    FragmentShaders fragment_shaders;

    std::unordered_map<ShaderTuple, OGLProgram, ShaderTuple::Hash> program_cache;
    OGLPipeline pipeline;
};

} // namespace GLShader
