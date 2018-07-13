// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_opengl/gl_shader_decompiler.h"
#include "video_core/renderer_opengl/gl_shader_gen.h"

namespace GLShader {

using Tegra::Engines::Maxwell3D;

static constexpr u32 PROGRAM_OFFSET{10};

ProgramResult GenerateVertexShader(const ShaderSetup& setup, const MaxwellVSConfig& config) {
    std::string out = "#version 430 core\n";
    out += "#extension GL_ARB_separate_shader_objects : enable\n\n";
    out += Decompiler::GetCommonDeclarations();
    out += "bool exec_vertex();\n";

    if (setup.IsDualProgram()) {
        out += "bool exec_vertex_b();\n";
    }

    ProgramResult program =
        Decompiler::DecompileProgram(setup.program.code, PROGRAM_OFFSET,
                                     Maxwell3D::Regs::ShaderStage::Vertex, "vertex")
            .get_value_or({});

    out += R"(

out gl_PerVertex {
    vec4 gl_Position;
};

out vec4 position;

layout (std140) uniform vs_config {
    vec4 viewport_flip;
};

void main() {
    exec_vertex();
)";

    if (setup.IsDualProgram()) {
        out += "    exec_vertex_b();";
    }

    out += R"(

    // Viewport can be flipped, which is unsupported by glViewport
    position.xy *= viewport_flip.xy;
    gl_Position = position;

    // TODO(bunnei): This is likely a hack, position.w should be interpolated as 1.0
    // For now, this is here to bring order in lieu of proper emulation
    position.w = 1.0;
}

)";

    out += program.first;

    if (setup.IsDualProgram()) {
        ProgramResult program_b =
            Decompiler::DecompileProgram(setup.program.code_b, PROGRAM_OFFSET,
                                         Maxwell3D::Regs::ShaderStage::Vertex, "vertex_b")
                .get_value_or({});
        out += program_b.first;
    }

    return {out, program.second};
}

ProgramResult GenerateFragmentShader(const ShaderSetup& setup, const MaxwellFSConfig& config) {
    std::string out = "#version 430 core\n";
    out += "#extension GL_ARB_separate_shader_objects : enable\n\n";
    out += Decompiler::GetCommonDeclarations();
    out += "bool exec_fragment();\n";

    ProgramResult program =
        Decompiler::DecompileProgram(setup.program.code, PROGRAM_OFFSET,
                                     Maxwell3D::Regs::ShaderStage::Fragment, "fragment")
            .get_value_or({});
    out += R"(
in vec4 position;
out vec4 color;

layout (std140) uniform fs_config {
    vec4 viewport_flip;
};

void main() {
    exec_fragment();
}

)";
    out += program.first;
    return {out, program.second};
}

} // namespace GLShader
