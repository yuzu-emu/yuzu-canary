// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <string>
#include <vector>

#include "common/common_types.h"
#include "video_core/engines/shader_bytecode.h"

namespace OpenGL::GLShader {

constexpr std::size_t MAX_PROGRAM_CODE_LENGTH{0x1000};
using ProgramCode = std::vector<u64>;

class ConstBufferEntry {
    using Maxwell = Tegra::Engines::Maxwell3D::Regs;

public:
    void MarkAsUsed(u64 index, u64 offset, Maxwell::ShaderStage stage) {
        is_used = true;
        this->index = static_cast<unsigned>(index);
        this->stage = stage;
        max_offset = std::max(max_offset, static_cast<unsigned>(offset));
    }

    void MarkAsUsedIndirect(u64 index, Maxwell::ShaderStage stage) {
        is_used = true;
        is_indirect = true;
        this->index = static_cast<unsigned>(index);
        this->stage = stage;
    }

    bool IsUsed() const {
        return is_used;
    }

    bool IsIndirect() const {
        return is_indirect;
    }

    unsigned GetIndex() const {
        return index;
    }

    unsigned GetSize() const {
        return max_offset + 1;
    }

    std::string GetName() const {
        return BufferBaseNames[static_cast<std::size_t>(stage)] + std::to_string(index);
    }

    u32 GetHash() const {
        return (static_cast<u32>(stage) << 16) | index;
    }

private:
    static constexpr std::array<const char*, Maxwell::MaxShaderStage> BufferBaseNames = {
        "buffer_vs_c", "buffer_tessc_c", "buffer_tesse_c", "buffer_gs_c", "buffer_fs_c",
    };

    bool is_used{};
    bool is_indirect{};
    unsigned index{};
    unsigned max_offset{};
    Maxwell::ShaderStage stage;
};

class SamplerEntry {
    using Maxwell = Tegra::Engines::Maxwell3D::Regs;

public:
    SamplerEntry(Maxwell::ShaderStage stage, std::size_t offset, std::size_t index,
                 Tegra::Shader::TextureType type, bool is_array, bool is_shadow)
        : offset(offset), stage(stage), sampler_index(index), type(type), is_array(is_array),
          is_shadow(is_shadow) {}

    std::size_t GetOffset() const {
        return offset;
    }

    std::size_t GetIndex() const {
        return sampler_index;
    }

    Maxwell::ShaderStage GetStage() const {
        return stage;
    }

    std::string GetName() const {
        return std::string(TextureSamplerNames[static_cast<std::size_t>(stage)]) + '_' +
               std::to_string(sampler_index);
    }

    std::string GetTypeString() const {
        using Tegra::Shader::TextureType;
        std::string glsl_type;

        switch (type) {
        case TextureType::Texture1D:
            glsl_type = "sampler1D";
            break;
        case TextureType::Texture2D:
            glsl_type = "sampler2D";
            break;
        case TextureType::Texture3D:
            glsl_type = "sampler3D";
            break;
        case TextureType::TextureCube:
            glsl_type = "samplerCube";
            break;
        default:
            UNIMPLEMENTED();
        }
        if (is_array)
            glsl_type += "Array";
        if (is_shadow)
            glsl_type += "Shadow";
        return glsl_type;
    }

    Tegra::Shader::TextureType GetType() const {
        return type;
    }

    bool IsArray() const {
        return is_array;
    }

    bool IsShadow() const {
        return is_shadow;
    }

    u32 GetHash() const {
        return (static_cast<u32>(stage) << 16) | static_cast<u32>(sampler_index);
    }

    static std::string GetArrayName(Maxwell::ShaderStage stage) {
        return TextureSamplerNames[static_cast<std::size_t>(stage)];
    }

private:
    static constexpr std::array<const char*, Maxwell::MaxShaderStage> TextureSamplerNames = {
        "tex_vs", "tex_tessc", "tex_tesse", "tex_gs", "tex_fs",
    };

    /// Offset in TSC memory from which to read the sampler object, as specified by the sampling
    /// instruction.
    std::size_t offset;
    Maxwell::ShaderStage stage;      ///< Shader stage where this sampler was used.
    std::size_t sampler_index;       ///< Value used to index into the generated GLSL sampler array.
    Tegra::Shader::TextureType type; ///< The type used to sample this texture (Texture2D, etc)
    bool is_array;  ///< Whether the texture is being sampled as an array texture or not.
    bool is_shadow; ///< Whether the texture is being sampled as a depth texture or not.
};

struct ShaderEntries {
    std::vector<ConstBufferEntry> const_buffer_entries;
    std::vector<SamplerEntry> texture_samplers;
};

using ProgramResult = std::pair<std::string, ShaderEntries>;

struct ShaderSetup {
    explicit ShaderSetup(ProgramCode program_code) {
        program.code = std::move(program_code);
    }

    struct {
        ProgramCode code;
        ProgramCode code_b; // Used for dual vertex shaders
    } program;

    /// Used in scenarios where we have a dual vertex shaders
    void SetProgramB(ProgramCode&& program_b) {
        program.code_b = std::move(program_b);
        has_program_b = true;
    }

    bool IsDualProgram() const {
        return has_program_b;
    }

private:
    bool has_program_b{};
};

/**
 * Generates the GLSL vertex shader program source code for the given VS program
 * @returns String of the shader source code
 */
ProgramResult GenerateVertexShader(const ShaderSetup& setup);

/**
 * Generates the GLSL geometry shader program source code for the given GS program
 * @returns String of the shader source code
 */
ProgramResult GenerateGeometryShader(const ShaderSetup& setup);

/**
 * Generates the GLSL fragment shader program source code for the given FS program
 * @returns String of the shader source code
 */
ProgramResult GenerateFragmentShader(const ShaderSetup& setup);

} // namespace OpenGL::GLShader
