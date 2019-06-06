// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>

#include <glad/glad.h>

#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_state.h"
#include "video_core/renderer_opengl/maxwell_to_gl.h"

namespace OpenGL::GLShader {

/// Uniform structure for the Uniform Buffer Object, all vectors must be 16-byte aligned
/// @note Always keep a vec4 at the end. The GL spec is not clear whether the alignment at
///       the end of a uniform block is included in UNIFORM_BLOCK_DATA_SIZE or not.
///       Not following that rule will cause problems on some AMD drivers.
struct MaxwellUniformData {
    void SetFromRegs(const Tegra::Engines::Maxwell3D& maxwell, std::size_t shader_stage);

    alignas(16) GLvec4 viewport_flip;
    struct alignas(16) {
        GLuint instance_id;
        GLuint flip_stage;
        GLfloat y_direction;
    };
};
static_assert(sizeof(MaxwellUniformData) == 32, "MaxwellUniformData structure size is incorrect");
static_assert(sizeof(MaxwellUniformData) < 16384,
              "MaxwellUniformData structure must be less than 16kb as per the OpenGL spec");

class ProgramManager {
public:
    explicit ProgramManager();
    ~ProgramManager();

    void ApplyTo(OpenGLState& state);

    void UseProgrammableVertexShader(GLuint program) {
        current_state.vertex_shader = program;
    }

    void UseProgrammableGeometryShader(GLuint program) {
        current_state.geometry_shader = program;
    }

    void UseProgrammableFragmentShader(GLuint program) {
        current_state.fragment_shader = program;
    }

    void UseTrivialGeometryShader() {
        current_state.geometry_shader = 0;
    }

private:
    struct PipelineState {
        bool operator==(const PipelineState& rhs) const {
            return vertex_shader == rhs.vertex_shader && fragment_shader == rhs.fragment_shader &&
                   geometry_shader == rhs.geometry_shader;
        }

        bool operator!=(const PipelineState& rhs) const {
            return !operator==(rhs);
        }

        GLuint vertex_shader{};
        GLuint fragment_shader{};
        GLuint geometry_shader{};
    };

    void UpdatePipeline();

    OGLPipeline pipeline;
    PipelineState current_state;
    PipelineState old_state;
};

} // namespace OpenGL::GLShader
