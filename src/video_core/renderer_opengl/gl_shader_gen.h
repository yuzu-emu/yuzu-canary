// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <string>
#include <vector>

#include "common/common_types.h"

namespace OpenGL::GLShader {

constexpr size_t MAX_PROGRAM_CODE_LENGTH{0x1000};
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
        return BufferBaseNames[static_cast<size_t>(stage)] + std::to_string(index);
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
    SamplerEntry(Maxwell::ShaderStage stage, size_t offset, size_t index)
        : offset(offset), stage(stage), sampler_index(index) {}

    size_t GetOffset() const {
        return offset;
    }

    size_t GetIndex() const {
        return sampler_index;
    }

    Maxwell::ShaderStage GetStage() const {
        return stage;
    }

    std::string GetName() const {
        return std::string(TextureSamplerNames[static_cast<size_t>(stage)]) + '[' +
               std::to_string(sampler_index) + ']';
    }

    static std::string GetArrayName(Maxwell::ShaderStage stage) {
        return TextureSamplerNames[static_cast<size_t>(stage)];
    }

private:
    static constexpr std::array<const char*, Maxwell::MaxShaderStage> TextureSamplerNames = {
        "tex_vs", "tex_tessc", "tex_tesse", "tex_gs", "tex_fs",
    };
    /// Offset in TSC memory from which to read the sampler object, as specified by the sampling
    /// instruction.
    size_t offset;
    Maxwell::ShaderStage stage; ///< Shader stage where this sampler was used.
    size_t sampler_index;       ///< Value used to index into the generated GLSL sampler array.
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
 * Generates the GLSL fragment shader program source code for the given FS program
 * @returns String of the shader source code
 */
ProgramResult GenerateFragmentShader(const ShaderSetup& setup);

} // namespace OpenGL::GLShader
