// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <fmt/format.h>
#include "common/assert.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_opengl/gl_shader_decompiler.h"
#include "video_core/renderer_opengl/gl_shader_gen.h"
#include "video_core/shader/shader_ir.h"

namespace OpenGL::GLShader {

using Tegra::Engines::Maxwell3D;
using VideoCommon::Shader::ProgramCode;
using VideoCommon::Shader::ShaderIR;

static constexpr u32 PROGRAM_OFFSET{10};

ProgramResult GenerateVertexShader(const ShaderSetup& setup) {
    const std::string id = fmt::format("{:016x}", setup.program.unique_identifier);

    std::string out = "#extension GL_ARB_separate_shader_objects : enable\n\n";
    out += "// Shader Unique Id: VS" + id + "\n\n";
    out += GetCommonDeclarations();

    out += R"(
layout (location = 0) out vec4 position;

layout (std140, binding = EMULATION_UBO_BINDING) uniform vs_config {
    vec4 viewport_flip;
    uvec4 config_pack; // instance_id, flip_stage, y_direction, padding
    uvec4 alpha_test;
};

)";
    ShaderIR program_ir(setup.program.code, PROGRAM_OFFSET);
    ProgramResult program = Decompile(program_ir, Maxwell3D::Regs::ShaderStage::Vertex, "vertex");

    out += program.first;

    if (setup.IsDualProgram()) {
        ShaderIR program_ir_b(setup.program.code_b, PROGRAM_OFFSET);
        ProgramResult program_b =
            Decompile(program_ir_b, Maxwell3D::Regs::ShaderStage::Vertex, "vertex_b");

        out += program_b.first;
    }

    out += R"(
void main() {
    position = vec4(0.0, 0.0, 0.0, 0.0);
    execute_vertex();
)";

    if (setup.IsDualProgram()) {
        out += "    execute_vertex_b();";
    }

    out += R"(
    // Check if the flip stage is VertexB
    // Config pack's second value is flip_stage
    if (config_pack[1] == 1) {
        // Viewport can be flipped, which is unsupported by glViewport
        position.xy *= viewport_flip.xy;
    }
    gl_Position = position;

    // TODO(bunnei): This is likely a hack, position.w should be interpolated as 1.0
    // For now, this is here to bring order in lieu of proper emulation
    if (config_pack[1] == 1) {
        position.w = 1.0;
    }
})";

    return {out, program.second};
}

ProgramResult GenerateGeometryShader(const ShaderSetup& setup) {
    const std::string id = fmt::format("{:016x}", setup.program.unique_identifier);

    std::string out = "#extension GL_ARB_separate_shader_objects : enable\n\n";
    out += "// Shader Unique Id: GS" + id + "\n\n";
    out += GetCommonDeclarations();

    out += R"(
layout (location = 0) in vec4 gs_position[];
layout (location = 0) out vec4 position;

layout (std140, binding = EMULATION_UBO_BINDING) uniform gs_config {
    vec4 viewport_flip;
    uvec4 config_pack; // instance_id, flip_stage, y_direction, padding
    uvec4 alpha_test;
};

)";
    ShaderIR program_ir(setup.program.code, PROGRAM_OFFSET);
    ProgramResult program =
        Decompile(program_ir, Maxwell3D::Regs::ShaderStage::Geometry, "geometry");
    out += program.first;

    out += R"(
void main() {
    execute_geometry();
};)";

    return {out, program.second};
}

ProgramResult GenerateFragmentShader(const ShaderSetup& setup) {
    const std::string id = fmt::format("{:016x}", setup.program.unique_identifier);

    std::string out = "#extension GL_ARB_separate_shader_objects : enable\n\n";
    out += "// Shader Unique Id: FS" + id + "\n\n";
    out += GetCommonDeclarations();

    out += R"(
layout (location = 0) out vec4 FragColor0;
layout (location = 1) out vec4 FragColor1;
layout (location = 2) out vec4 FragColor2;
layout (location = 3) out vec4 FragColor3;
layout (location = 4) out vec4 FragColor4;
layout (location = 5) out vec4 FragColor5;
layout (location = 6) out vec4 FragColor6;
layout (location = 7) out vec4 FragColor7;

layout (location = 0) in noperspective vec4 position;

layout (std140, binding = EMULATION_UBO_BINDING) uniform fs_config {
    vec4 viewport_flip;
    uvec4 config_pack; // instance_id, flip_stage, y_direction, padding
    uvec4 alpha_test;
};

bool AlphaFunc(in float value) {
    float ref = uintBitsToFloat(alpha_test[2]);
    switch (alpha_test[1]) {
        case 1:
            return false;
        case 2:
            return value < ref;
        case 3:
            return value == ref;
        case 4:
            return value <= ref;
        case 5:
            return value > ref;
        case 6:
            return value != ref;
        case 7:
            return value >= ref;
        case 8:
            return true;
        default:
            return false;
    }
}

)";
    ShaderIR program_ir(setup.program.code, PROGRAM_OFFSET);
    ProgramResult program =
        Decompile(program_ir, Maxwell3D::Regs::ShaderStage::Fragment, "fragment");

    out += program.first;

    out += R"(
void main() {
    execute_fragment();
}

)";
    return {out, program.second};
}

} // namespace OpenGL::GLShader
