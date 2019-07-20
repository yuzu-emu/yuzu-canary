// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <vector>

#include "common/common_types.h"
#include "video_core/renderer_opengl/gl_shader_decompiler.h"
#include "video_core/shader/shader_ir.h"

namespace OpenGL {
class Device;
}

namespace OpenGL::GLShader {

using VideoCommon::Shader::ProgramCode;

struct ShaderSetup {
    explicit ShaderSetup(ProgramCode program_code) {
        program.code = std::move(program_code);
    }

    struct {
        ProgramCode code;
        ProgramCode code_b; // Used for dual vertex shaders
        u64 unique_identifier;
        std::size_t size_a;
        std::size_t size_b;
    } program;

    /// Used in scenarios where we have a dual vertex shaders
    void SetProgramB(ProgramCode program_b) {
        program.code_b = std::move(program_b);
        has_program_b = true;
    }

    bool IsDualProgram() const {
        return has_program_b;
    }

private:
    bool has_program_b{};
};

/// Generates the GLSL vertex shader program source code for the given VS program
ProgramResult GenerateVertexShader(const Device& device, const ShaderSetup& setup);

/// Generates the GLSL geometry shader program source code for the given GS program
ProgramResult GenerateGeometryShader(const Device& device, const ShaderSetup& setup);

/// Generates the GLSL fragment shader program source code for the given FS program
ProgramResult GenerateFragmentShader(const Device& device, const ShaderSetup& setup);

/// Generates the GLSL compute shader program source code for the given CS program
ProgramResult GenerateComputeShader(const Device& device, const ShaderSetup& setup);

} // namespace OpenGL::GLShader
