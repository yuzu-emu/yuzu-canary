// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <glad/glad.h>

#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/maxwell_to_gl.h"

namespace OpenGL::GLShader {

/// Number of OpenGL texture samplers that can be used in the fragment shader
static constexpr size_t NumTextureSamplers = 32;

using Tegra::Engines::Maxwell3D;

/// Uniform structure for the Uniform Buffer Object, all vectors must be 16-byte aligned
// NOTE: Always keep a vec4 at the end. The GL spec is not clear whether the alignment at
//       the end of a uniform block is included in UNIFORM_BLOCK_DATA_SIZE or not.
//       Not following that rule will cause problems on some AMD drivers.
struct MaxwellUniformData {
    void SetFromRegs(const Maxwell3D::State::ShaderStageInfo& shader_stage);
    alignas(16) GLvec4 viewport_flip;
    alignas(16) GLuvec4 instance_id;
};
static_assert(sizeof(MaxwellUniformData) == 32, "MaxwellUniformData structure size is incorrect");
static_assert(sizeof(MaxwellUniformData) < 16384,
              "MaxwellUniformData structure must be less than 16kb as per the OpenGL spec");

class ProgramManager {
public:
    ProgramManager() {
        pipeline.Create();
    }

    void UseProgrammableVertexShader(GLuint program) {
        vs = program;
    }

    void UseProgrammableFragmentShader(GLuint program) {
        fs = program;
    }

    void UseTrivialGeometryShader() {
        gs = 0;
    }

    void ApplyTo(OpenGLState& state) {
        // Workaround for AMD bug
        glUseProgramStages(pipeline.handle,
                           GL_VERTEX_SHADER_BIT | GL_GEOMETRY_SHADER_BIT | GL_FRAGMENT_SHADER_BIT,
                           0);

        glUseProgramStages(pipeline.handle, GL_VERTEX_SHADER_BIT, vs);
        glUseProgramStages(pipeline.handle, GL_GEOMETRY_SHADER_BIT, gs);
        glUseProgramStages(pipeline.handle, GL_FRAGMENT_SHADER_BIT, fs);
        state.draw.shader_program = 0;
        state.draw.program_pipeline = pipeline.handle;
    }

private:
    OGLPipeline pipeline;
    GLuint vs{}, fs{}, gs{};
};

} // namespace OpenGL::GLShader
