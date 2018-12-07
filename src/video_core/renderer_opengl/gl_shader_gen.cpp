// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <fmt/format.h>
#include "common/assert.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_opengl/gl_shader_decompiler.h"
#include "video_core/renderer_opengl/gl_shader_gen.h"

namespace OpenGL::GLShader {

using Tegra::Engines::Maxwell3D;

static constexpr u32 PROGRAM_OFFSET{10};

ProgramResult GenerateVertexShader(const ShaderSetup& setup) {
    std::string out = "#version 430 core\n";
    out += "#extension GL_ARB_separate_shader_objects : enable\n\n";
    const std::string id = fmt::format("{:016x}", setup.program.unique_identifier);
    out += "// Shader Unique Id: VS" + id + "\n\n";
    out += Decompiler::GetCommonDeclarations();

    out += R"(

layout (location = 0) out vec4 position;

layout(std140) uniform vs_config {
    vec4 viewport_flip;
    uvec4 config_pack; // instance_id, flip_stage, y_direction, padding
    uvec4 alpha_test;
};
)";

    if (setup.IsDualProgram()) {
        out += "bool exec_vertex_b();\n";
    }

    ProgramResult program =
        Decompiler::DecompileProgram(setup.program.code, PROGRAM_OFFSET,
                                     Maxwell3D::Regs::ShaderStage::Vertex, "vertex")
            .value_or(ProgramResult());

    out += program.first;

    if (setup.IsDualProgram()) {
        ProgramResult program_b =
            Decompiler::DecompileProgram(setup.program.code_b, PROGRAM_OFFSET,
                                         Maxwell3D::Regs::ShaderStage::Vertex, "vertex_b")
                .value_or(ProgramResult());
        out += program_b.first;
    }

    out += R"(

void main() {
    position = vec4(0.0, 0.0, 0.0, 0.0);
    exec_vertex();
)";

    if (setup.IsDualProgram()) {
        out += "    exec_vertex_b();";
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
}

)";

    return {out, program.second};
}

ProgramResult GenerateGeometryShader(const ShaderSetup& setup) {
    // Version is intentionally skipped in shader generation, it's added by the lazy compilation.
    std::string out = "#extension GL_ARB_separate_shader_objects : enable\n\n";
    const std::string id = fmt::format("{:016x}", setup.program.unique_identifier);
    out += "// Shader Unique Id: GS" + id + "\n\n";
    out += Decompiler::GetCommonDeclarations();
    out += "bool exec_geometry();\n";

    ProgramResult program =
        Decompiler::DecompileProgram(setup.program.code, PROGRAM_OFFSET,
                                     Maxwell3D::Regs::ShaderStage::Geometry, "geometry")
            .value_or(ProgramResult());
    out += R"(
out gl_PerVertex {
    vec4 gl_Position;
};

layout (location = 0) in vec4 gs_position[];
layout (location = 0) out vec4 position;

layout (std140) uniform gs_config {
    vec4 viewport_flip;
    uvec4 config_pack; // instance_id, flip_stage, y_direction, padding
    uvec4 alpha_test;
};

void main() {
    exec_geometry();
}

)";
    out += program.first;
    return {out, program.second};
}

ProgramResult GenerateFragmentShader(const ShaderSetup& setup) {
    std::string out = "#version 430 core\n";
    out += "#extension GL_ARB_separate_shader_objects : enable\n\n";
    const std::string id = fmt::format("{:016x}", setup.program.unique_identifier);
    out += "// Shader Unique Id: FS" + id + "\n\n";
    out += Decompiler::GetCommonDeclarations();
    out += "bool exec_fragment();\n";

    ProgramResult program =
        Decompiler::DecompileProgram(setup.program.code, PROGRAM_OFFSET,
                                     Maxwell3D::Regs::ShaderStage::Fragment, "fragment")
            .value_or(ProgramResult());
    out += R"(
layout(location = 0) out vec4 FragColor0;
layout(location = 1) out vec4 FragColor1;
layout(location = 2) out vec4 FragColor2;
layout(location = 3) out vec4 FragColor3;
layout(location = 4) out vec4 FragColor4;
layout(location = 5) out vec4 FragColor5;
layout(location = 6) out vec4 FragColor6;
layout(location = 7) out vec4 FragColor7;

layout (location = 0) in vec4 position;

layout (std140) uniform fs_config {
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

void main() {
    exec_fragment();
}

)";
    out += program.first;
    return {out, program.second};
}
} // namespace OpenGL::GLShader
