// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include "common/common_types.h"
#include "common/hash.h"

namespace GLShader {

constexpr size_t MAX_PROGRAM_CODE_LENGTH{0x1000};

using ProgramCode = std::array<u64, MAX_PROGRAM_CODE_LENGTH>;

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
    ShaderSetup(const ProgramCode& program_code) {
        program.code = program_code;
    }

    struct {
        ProgramCode code;
        ProgramCode code_b; // Used for dual vertex shaders
    } program;

    bool program_code_hash_dirty = true;

    u64 GetProgramCodeHash() {
        if (program_code_hash_dirty) {
            program_code_hash = GetNewHash();
            program_code_hash_dirty = false;
        }
        return program_code_hash;
    }

    /// Used in scenarios where we have a dual vertex shaders
    void SetProgramB(const ProgramCode& program_b) {
        program.code_b = program_b;
        has_program_b = true;
    }

    bool IsDualProgram() const {
        return has_program_b;
    }

private:
    u64 GetNewHash() const {
        if (has_program_b) {
            // Compute hash over dual shader programs
            return Common::ComputeHash64(&program, sizeof(program));
        } else {
            // Compute hash over a single shader program
            return Common::ComputeHash64(&program.code, program.code.size());
        }
    }

    u64 program_code_hash{};
    bool has_program_b{};
};

struct MaxwellShaderConfigCommon {
    void Init(ShaderSetup& setup) {
        program_hash = setup.GetProgramCodeHash();
    }

    u64 program_hash;
};

struct MaxwellVSConfig : Common::HashableStruct<MaxwellShaderConfigCommon> {
    explicit MaxwellVSConfig(ShaderSetup& setup) {
        state.Init(setup);
    }
};

struct MaxwellFSConfig : Common::HashableStruct<MaxwellShaderConfigCommon> {
    explicit MaxwellFSConfig(ShaderSetup& setup) {
        state.Init(setup);
    }
};

/**
 * Generates the GLSL vertex shader program source code for the given VS program
 * @returns String of the shader source code
 */
ProgramResult GenerateVertexShader(const ShaderSetup& setup, const MaxwellVSConfig& config);

/**
 * Generates the GLSL fragment shader program source code for the given FS program
 * @returns String of the shader source code
 */
ProgramResult GenerateFragmentShader(const ShaderSetup& setup, const MaxwellFSConfig& config);

} // namespace GLShader

namespace std {

template <>
struct hash<GLShader::MaxwellVSConfig> {
    size_t operator()(const GLShader::MaxwellVSConfig& k) const {
        return k.Hash();
    }
};

template <>
struct hash<GLShader::MaxwellFSConfig> {
    size_t operator()(const GLShader::MaxwellFSConfig& k) const {
        return k.Hash();
    }
};

} // namespace std
