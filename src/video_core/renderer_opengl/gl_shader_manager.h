// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <tuple>

#include <glad/glad.h>

#include "common/common_types.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_state.h"
#include "video_core/renderer_opengl/maxwell_to_gl.h"

namespace Tegra::Engines {
class Maxwell3D;
}

namespace OpenGL::GLShader {

class StageProgram final : public OGLProgram {
public:
    explicit StageProgram();
    ~StageProgram();

    void UpdateConstants();

    void SetInstanceID(GLuint instance_id) {
        state.instance_id = instance_id;
    }

    void SetFlipStage(GLuint flip_stage) {
        state.flip_stage = flip_stage;
    }

    void SetYDirection(GLfloat y_direction) {
        state.y_direction = y_direction;
    }

    void SetRescalingFactor(GLfloat rescaling_factor) {
        state.rescaling_factor = rescaling_factor;
    }

    void SetViewportScale(GLfloat x, GLfloat y) {
        state.viewport_scale = {x, y};
    }

private:
    struct State {
        union {
            std::array<GLuint, 4> config_pack{};
            struct {
                GLuint instance_id;
                GLuint flip_stage;
                GLfloat y_direction;
                GLfloat rescaling_factor;
            };
        };

        std::array<GLfloat, 2> viewport_scale{};
    };

    State state;
    State old_state;
};

class ProgramManager final {
public:
    explicit ProgramManager();
    ~ProgramManager();

    void SetConstants(Tegra::Engines::Maxwell3D& maxwell_3d, bool rescaling);

    void ApplyTo(OpenGLState& state);

    void BindVertexShader(StageProgram* program) {
        current_state.vertex = program;
    }

    void BindGeometryShader(StageProgram* program) {
        current_state.geometry = program;
    }

    void BindFragmentShader(StageProgram* program) {
        current_state.fragment = program;
    }

private:
    struct PipelineState {
        bool operator==(const PipelineState& rhs) const {
            return vertex == rhs.vertex && fragment == rhs.fragment && geometry == rhs.geometry;
        }

        bool operator!=(const PipelineState& rhs) const {
            return !operator==(rhs);
        }

        StageProgram* vertex{};
        StageProgram* fragment{};
        StageProgram* geometry{};
    };

    void UpdatePipeline();

    OGLPipeline pipeline;
    PipelineState current_state;
    PipelineState old_state;
};

} // namespace OpenGL::GLShader
