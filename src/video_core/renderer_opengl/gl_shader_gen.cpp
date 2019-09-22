// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <fmt/format.h>
#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_opengl/gl_shader_decompiler.h"
#include "video_core/renderer_opengl/gl_shader_gen.h"
#include "video_core/shader/shader_ir.h"

namespace OpenGL::GLShader {

using Tegra::Engines::Maxwell3D;
using VideoCommon::Shader::ProgramCode;
using VideoCommon::Shader::ShaderIR;

static constexpr u32 PROGRAM_OFFSET = 10;
static constexpr u32 COMPUTE_OFFSET = 0;

ProgramResult GenerateVertexShader(const Device& device, const ShaderSetup& setup) {
    const std::string id = fmt::format("{:016x}", setup.program.unique_identifier);

    std::string out = "// Shader Unique Id: VS" + id + "\n\n";
    out += GetCommonDeclarations();

    const ShaderIR program_ir(setup.program.code, PROGRAM_OFFSET, setup.program.size_a);
    const auto stage = setup.IsDualProgram() ? ProgramType::VertexA : ProgramType::VertexB;
    ProgramResult program = Decompile(device, program_ir, stage, "vertex");
    out += program.first;

    if (setup.IsDualProgram()) {
        const ShaderIR program_ir_b(setup.program.code_b, PROGRAM_OFFSET, setup.program.size_b);
        ProgramResult program_b = Decompile(device, program_ir_b, ProgramType::VertexB, "vertex_b");
        out += program_b.first;
    }

    out += R"(
void main() {
    execute_vertex();
)";

    if (setup.IsDualProgram()) {
        out += "    execute_vertex_b();";
    }

    out += R"(

    // Set Position Y direction
    gl_Position.y *= utof(config_pack[2]);
    // Check if the flip stage is VertexB
    // Config pack's second value is flip_stage
    if (config_pack[1] == 1) {
        // Viewport can be flipped, which is unsupported by glViewport
        gl_Position.xy *= viewport_flip.xy;
    }
})";

    return {std::move(out), std::move(program.second)};
}

ProgramResult GenerateGeometryShader(const Device& device, const ShaderSetup& setup) {
    const std::string id = fmt::format("{:016x}", setup.program.unique_identifier);

    std::string out = "// Shader Unique Id: GS" + id + "\n\n";
    out += GetCommonDeclarations();

    const ShaderIR program_ir(setup.program.code, PROGRAM_OFFSET, setup.program.size_a);
    ProgramResult program = Decompile(device, program_ir, ProgramType::Geometry, "geometry");
    out += program.first;

    out += R"(
void main() {
    execute_geometry();
})";

    return {std::move(out), std::move(program.second)};
}

ProgramResult GenerateFragmentShader(const Device& device, const ShaderSetup& setup) {
    const std::string id = fmt::format("{:016x}", setup.program.unique_identifier);

    std::string out = "// Shader Unique Id: FS" + id + "\n\n";
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

)";
    const ShaderIR program_ir(setup.program.code, PROGRAM_OFFSET, setup.program.size_a);
    ProgramResult program = Decompile(device, program_ir, ProgramType::Fragment, "fragment");
    out += program.first;

    out += R"(
void main() {
    execute_fragment();
}

)";
    return {std::move(out), std::move(program.second)};
}

ProgramResult GenerateComputeShader(const Device& device, const ShaderSetup& setup) {
    const std::string id = fmt::format("{:016x}", setup.program.unique_identifier);

    std::string out = "// Shader Unique Id: CS" + id + "\n\n";
    out += GetCommonDeclarations();

    const ShaderIR program_ir(setup.program.code, COMPUTE_OFFSET, setup.program.size_a);
    ProgramResult program = Decompile(device, program_ir, ProgramType::Compute, "compute");
    out += program.first;

    out += R"(
void main() {
    execute_compute();
}
)";
    return {std::move(out), std::move(program.second)};
}

} // namespace OpenGL::GLShader
