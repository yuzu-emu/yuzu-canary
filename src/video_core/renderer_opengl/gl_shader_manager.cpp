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
}

} // namespace OpenGL::GLShader
