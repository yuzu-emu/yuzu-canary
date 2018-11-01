// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <iterator>
#include <glad/glad.h>
#include "common/assert.h"
#include "common/logging/log.h"
#include "video_core/renderer_opengl/gl_state.h"

namespace OpenGL {

OpenGLState OpenGLState::cur_state;
bool OpenGLState::s_rgb_used;
OpenGLState::OpenGLState() {
    // These all match default OpenGL values
    framebuffer_srgb.enabled = false;
    cull.enabled = false;
    cull.mode = GL_BACK;
    cull.front_face = GL_CCW;

    depth.test_enabled = false;
    depth.test_func = GL_LESS;
    depth.write_mask = GL_TRUE;
    depth.depth_range_near = 0.0f;
    depth.depth_range_far = 1.0f;

    primitive_restart.enabled = false;
    primitive_restart.index = 0;

    color_mask.red_enabled = GL_TRUE;
    color_mask.green_enabled = GL_TRUE;
    color_mask.blue_enabled = GL_TRUE;
    color_mask.alpha_enabled = GL_TRUE;

    stencil.test_enabled = false;
    auto reset_stencil = [](auto& config) {
        config.test_func = GL_ALWAYS;
        config.test_ref = 0;
        config.test_mask = 0xFFFFFFFF;
        config.write_mask = 0xFFFFFFFF;
        config.action_depth_fail = GL_KEEP;
        config.action_depth_pass = GL_KEEP;
        config.action_stencil_fail = GL_KEEP;
    };
    reset_stencil(stencil.front);
    reset_stencil(stencil.back);

    blend.enabled = true;
    blend.rgb_equation = GL_FUNC_ADD;
    blend.a_equation = GL_FUNC_ADD;
    blend.src_rgb_func = GL_ONE;
    blend.dst_rgb_func = GL_ZERO;
    blend.src_a_func = GL_ONE;
    blend.dst_a_func = GL_ZERO;
    blend.color.red = 0.0f;
    blend.color.green = 0.0f;
    blend.color.blue = 0.0f;
    blend.color.alpha = 0.0f;

    logic_op.enabled = false;
    logic_op.operation = GL_COPY;

    for (auto& texture_unit : texture_units) {
        texture_unit.Reset();
    }

    draw.read_framebuffer = 0;
    draw.draw_framebuffer = 0;
    draw.vertex_array = 0;
    draw.vertex_buffer = 0;
    draw.uniform_buffer = 0;
    draw.shader_program = 0;
    draw.program_pipeline = 0;

    scissor.enabled = false;
    scissor.x = 0;
    scissor.y = 0;
    scissor.width = 0;
    scissor.height = 0;

    viewport.x = 0;
    viewport.y = 0;
    viewport.width = 0;
    viewport.height = 0;

    clip_distance = {};

    point.size = 1;
}

void OpenGLState::ApplyDefaultState() {
    glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_PRIMITIVE_RESTART);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_BLEND);
    glDisable(GL_COLOR_LOGIC_OP);
    glDisable(GL_SCISSOR_TEST);
}

void OpenGLState::ApplySRgb() const {
    // sRGB
    if (framebuffer_srgb.enabled != cur_state.framebuffer_srgb.enabled) {
        if (framebuffer_srgb.enabled) {
            // Track if sRGB is used
            s_rgb_used = true;
            glEnable(GL_FRAMEBUFFER_SRGB);
        } else {
            glDisable(GL_FRAMEBUFFER_SRGB);
        }
    }
}

void OpenGLState::ApplyCulling() const {
    // Culling
    const bool cull_changed = cull.enabled != cur_state.cull.enabled;
    if (cull_changed) {
        if (cull.enabled) {
            glEnable(GL_CULL_FACE);
        } else {
            glDisable(GL_CULL_FACE);
        }
    }
    if (cull.enabled) {
        if (cull_changed || cull.mode != cur_state.cull.mode) {
            glCullFace(cull.mode);
        }

        if (cull_changed || cull.front_face != cur_state.cull.front_face) {
            glFrontFace(cull.front_face);
        }
    }
}

void OpenGLState::ApplyDepth() const {
    // Depth test
    const bool depth_test_changed = depth.test_enabled != cur_state.depth.test_enabled;
    if (depth_test_changed) {
        if (depth.test_enabled) {
            glEnable(GL_DEPTH_TEST);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
    }
    if (depth.test_enabled &&
        (depth_test_changed || depth.test_func != cur_state.depth.test_func)) {
        glDepthFunc(depth.test_func);
    }
    // Depth mask
    if (depth.write_mask != cur_state.depth.write_mask) {
        glDepthMask(depth.write_mask);
    }
    // Depth range
    if (depth.depth_range_near != cur_state.depth.depth_range_near ||
        depth.depth_range_far != cur_state.depth.depth_range_far) {
        glDepthRange(depth.depth_range_near, depth.depth_range_far);
    }
}

void OpenGLState::ApplyPrimitiveRestart() const {
    const bool primitive_restart_changed =
        primitive_restart.enabled != cur_state.primitive_restart.enabled;
    if (primitive_restart_changed) {
        if (primitive_restart.enabled) {
            glEnable(GL_PRIMITIVE_RESTART);
        } else {
            glDisable(GL_PRIMITIVE_RESTART);
        }
    }
    if (primitive_restart_changed ||
        (primitive_restart.enabled &&
         primitive_restart.index != cur_state.primitive_restart.index)) {
        glPrimitiveRestartIndex(primitive_restart.index);
    }
}

void OpenGLState::ApplyStencilTest() const {
    const bool stencil_test_changed = stencil.test_enabled != cur_state.stencil.test_enabled;
    if (stencil_test_changed) {
        if (stencil.test_enabled) {
            glEnable(GL_STENCIL_TEST);
        } else {
            glDisable(GL_STENCIL_TEST);
        }
    }
    if (stencil.test_enabled) {
        auto config_stencil = [stencil_test_changed](GLenum face, const auto& config,
                                                     const auto& prev_config) {
            if (stencil_test_changed || config.test_func != prev_config.test_func ||
                config.test_ref != prev_config.test_ref ||
                config.test_mask != prev_config.test_mask) {
                glStencilFuncSeparate(face, config.test_func, config.test_ref, config.test_mask);
            }
            if (stencil_test_changed || config.action_depth_fail != prev_config.action_depth_fail ||
                config.action_depth_pass != prev_config.action_depth_pass ||
                config.action_stencil_fail != prev_config.action_stencil_fail) {
                glStencilOpSeparate(face, config.action_stencil_fail, config.action_depth_fail,
                                    config.action_depth_pass);
            }
            if (config.write_mask != prev_config.write_mask) {
                glStencilMaskSeparate(face, config.write_mask);
            }
        };
        config_stencil(GL_FRONT, stencil.front, cur_state.stencil.front);
        config_stencil(GL_BACK, stencil.back, cur_state.stencil.back);
    }
}

void OpenGLState::ApplyScissorTest() const {
    const bool scissor_changed = scissor.enabled != cur_state.scissor.enabled;
    if (scissor_changed) {
        if (scissor.enabled) {
            glEnable(GL_SCISSOR_TEST);
        } else {
            glDisable(GL_SCISSOR_TEST);
        }
    }
    if (scissor_changed || scissor_changed || scissor.x != cur_state.scissor.x ||
        scissor.y != cur_state.scissor.y || scissor.width != cur_state.scissor.width ||
        scissor.height != cur_state.scissor.height) {
        glScissor(scissor.x, scissor.y, scissor.width, scissor.height);
    }
}

void OpenGLState::ApplyBlending() const {
    const bool blend_changed = blend.enabled != cur_state.blend.enabled;
    if (blend_changed) {
        if (blend.enabled) {
            ASSERT(!logic_op.enabled);
            glEnable(GL_BLEND);
        } else {
            glDisable(GL_BLEND);
        }
    }
    if (blend.enabled) {
        if (blend_changed || blend.color.red != cur_state.blend.color.red ||
            blend.color.green != cur_state.blend.color.green ||
            blend.color.blue != cur_state.blend.color.blue ||
            blend.color.alpha != cur_state.blend.color.alpha) {
            glBlendColor(blend.color.red, blend.color.green, blend.color.blue, blend.color.alpha);
        }

        if (blend_changed || blend.src_rgb_func != cur_state.blend.src_rgb_func ||
            blend.dst_rgb_func != cur_state.blend.dst_rgb_func ||
            blend.src_a_func != cur_state.blend.src_a_func ||
            blend.dst_a_func != cur_state.blend.dst_a_func) {
            glBlendFuncSeparate(blend.src_rgb_func, blend.dst_rgb_func, blend.src_a_func,
                                blend.dst_a_func);
        }

        if (blend_changed || blend.rgb_equation != cur_state.blend.rgb_equation ||
            blend.a_equation != cur_state.blend.a_equation) {
            glBlendEquationSeparate(blend.rgb_equation, blend.a_equation);
        }
    }
}

void OpenGLState::ApplyLogicOp() const {
    const bool logic_op_changed = logic_op.enabled != cur_state.logic_op.enabled;
    if (logic_op_changed) {
        if (logic_op.enabled) {
            ASSERT(!blend.enabled);
            glEnable(GL_COLOR_LOGIC_OP);
        } else {
            glDisable(GL_COLOR_LOGIC_OP);
        }
    }

    if (logic_op.enabled &&
        (logic_op_changed || logic_op.operation != cur_state.logic_op.operation)) {
        glLogicOp(logic_op.operation);
    }
}

void OpenGLState::ApplyTextures() const {
    for (std::size_t i = 0; i < std::size(texture_units); ++i) {
        const auto& texture_unit = texture_units[i];
        const auto& cur_state_texture_unit = cur_state.texture_units[i];

        if (texture_unit.texture != cur_state_texture_unit.texture) {
            glActiveTexture(TextureUnits::MaxwellTexture(static_cast<int>(i)).Enum());
            glBindTexture(texture_unit.target, texture_unit.texture);
        }
        // Update the texture swizzle
        if (texture_unit.swizzle.r != cur_state_texture_unit.swizzle.r ||
            texture_unit.swizzle.g != cur_state_texture_unit.swizzle.g ||
            texture_unit.swizzle.b != cur_state_texture_unit.swizzle.b ||
            texture_unit.swizzle.a != cur_state_texture_unit.swizzle.a) {
            std::array<GLint, 4> mask = {texture_unit.swizzle.r, texture_unit.swizzle.g,
                                         texture_unit.swizzle.b, texture_unit.swizzle.a};
            glTexParameteriv(texture_unit.target, GL_TEXTURE_SWIZZLE_RGBA, mask.data());
        }
    }
}

void OpenGLState::ApplySamplers() const {
    bool has_delta{};
    std::size_t first{}, last{};
    std::array<GLuint, Tegra::Engines::Maxwell3D::Regs::NumTextureSamplers> samplers;
    for (std::size_t i = 0; i < std::size(samplers); ++i) {
        samplers[i] = texture_units[i].sampler;
        if (samplers[i] != cur_state.texture_units[i].sampler) {
            if (!has_delta) {
                first = i;
                has_delta = true;
            }
            last = i;
        }
    }
    if (has_delta) {
        glBindSamplers(static_cast<GLuint>(first), static_cast<GLsizei>(last - first + 1),
                       samplers.data());
    }
}

void OpenGLState::Apply() const {
    // Framebuffer
    if (draw.read_framebuffer != cur_state.draw.read_framebuffer) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, draw.read_framebuffer);
    }
    if (draw.draw_framebuffer != cur_state.draw.draw_framebuffer) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw.draw_framebuffer);
    }

    // Vertex array
    if (draw.vertex_array != cur_state.draw.vertex_array) {
        glBindVertexArray(draw.vertex_array);
    }

    // Vertex buffer
    if (draw.vertex_buffer != cur_state.draw.vertex_buffer) {
        glBindBuffer(GL_ARRAY_BUFFER, draw.vertex_buffer);
    }

    // Uniform buffer
    if (draw.uniform_buffer != cur_state.draw.uniform_buffer) {
        glBindBuffer(GL_UNIFORM_BUFFER, draw.uniform_buffer);
    }

    // Shader program
    if (draw.shader_program != cur_state.draw.shader_program) {
        glUseProgram(draw.shader_program);
    }

    // Program pipeline
    if (draw.program_pipeline != cur_state.draw.program_pipeline) {
        glBindProgramPipeline(draw.program_pipeline);
    }
    // Viewport
    if (viewport.x != cur_state.viewport.x || viewport.y != cur_state.viewport.y ||
        viewport.width != cur_state.viewport.width ||
        viewport.height != cur_state.viewport.height) {
        glViewport(viewport.x, viewport.y, viewport.width, viewport.height);
    }
    // Clip distance
    for (std::size_t i = 0; i < clip_distance.size(); ++i) {
        if (clip_distance[i] != cur_state.clip_distance[i]) {
            if (clip_distance[i]) {
                glEnable(GL_CLIP_DISTANCE0 + static_cast<GLenum>(i));
            } else {
                glDisable(GL_CLIP_DISTANCE0 + static_cast<GLenum>(i));
            }
        }
    }
    // Color mask
    if (color_mask.red_enabled != cur_state.color_mask.red_enabled ||
        color_mask.green_enabled != cur_state.color_mask.green_enabled ||
        color_mask.blue_enabled != cur_state.color_mask.blue_enabled ||
        color_mask.alpha_enabled != cur_state.color_mask.alpha_enabled) {
        glColorMask(color_mask.red_enabled, color_mask.green_enabled, color_mask.blue_enabled,
                    color_mask.alpha_enabled);
    }
    // Point
    if (point.size != cur_state.point.size) {
        glPointSize(point.size);
    }
    ApplyScissorTest();
    ApplyStencilTest();
    ApplySRgb();
    ApplyCulling();
    ApplyDepth();
    ApplyPrimitiveRestart();
    ApplyBlending();
    ApplyLogicOp();
    ApplyTextures();
    ApplySamplers();
    cur_state = *this;
}

OpenGLState& OpenGLState::UnbindTexture(GLuint handle) {
    for (auto& unit : texture_units) {
        if (unit.texture == handle) {
            unit.Unbind();
        }
    }
    return *this;
}

OpenGLState& OpenGLState::ResetSampler(GLuint handle) {
    for (auto& unit : texture_units) {
        if (unit.sampler == handle) {
            unit.sampler = 0;
        }
    }
    return *this;
}

OpenGLState& OpenGLState::ResetProgram(GLuint handle) {
    if (draw.shader_program == handle) {
        draw.shader_program = 0;
    }
    return *this;
}

OpenGLState& OpenGLState::ResetPipeline(GLuint handle) {
    if (draw.program_pipeline == handle) {
        draw.program_pipeline = 0;
    }
    return *this;
}

OpenGLState& OpenGLState::ResetBuffer(GLuint handle) {
    if (draw.vertex_buffer == handle) {
        draw.vertex_buffer = 0;
    }
    if (draw.uniform_buffer == handle) {
        draw.uniform_buffer = 0;
    }
    return *this;
}

OpenGLState& OpenGLState::ResetVertexArray(GLuint handle) {
    if (draw.vertex_array == handle) {
        draw.vertex_array = 0;
    }
    return *this;
}

OpenGLState& OpenGLState::ResetFramebuffer(GLuint handle) {
    if (draw.read_framebuffer == handle) {
        draw.read_framebuffer = 0;
    }
    if (draw.draw_framebuffer == handle) {
        draw.draw_framebuffer = 0;
    }
    return *this;
}

} // namespace OpenGL
