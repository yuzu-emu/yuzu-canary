// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <string>
#include <vector>

#include "common/common_types.h"
#include "video_core/engines/shader_bytecode.h"
#include "video_core/renderer_opengl/gl_shader_decompiler.h"
#include "video_core/shader/shader_ir.h"

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
