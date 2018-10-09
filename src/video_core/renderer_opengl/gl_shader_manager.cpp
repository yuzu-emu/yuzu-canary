// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/core.h"
#include "video_core/renderer_opengl/gl_shader_manager.h"

namespace OpenGL::GLShader {

void MaxwellUniformData::SetFromRegs(const Maxwell3D::State::ShaderStageInfo& shader_stage) {
    const auto& gpu = Core::System::GetInstance().GPU().Maxwell3D();
    const auto& regs = gpu.regs;
    const auto& state = gpu.state;

    // TODO(bunnei): Support more than one viewport
    viewport_flip[0] = regs.viewport_transform[0].scale_x < 0.0 ? -1.0f : 1.0f;
    viewport_flip[1] = regs.viewport_transform[0].scale_y < 0.0 ? -1.0f : 1.0f;

    // We only assign the instance to the first component of the vector, the rest is just padding.
    instance_id[0] = state.current_instance;

    // Assign in which stage the position has to be flipped
    // (the last stage before the fragment shader).
    if (gpu.regs.shader_config[static_cast<u32>(Maxwell3D::Regs::ShaderProgram::Geometry)].enable) {
        flip_stage[0] = static_cast<u32>(Maxwell3D::Regs::ShaderProgram::Geometry);
    } else {
        flip_stage[0] = static_cast<u32>(Maxwell3D::Regs::ShaderProgram::VertexB);
    }
}

} // namespace OpenGL::GLShader
