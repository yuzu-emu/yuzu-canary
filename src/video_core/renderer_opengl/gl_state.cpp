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
    geometry_shaders.enabled = false;
    framebuffer_srgb.enabled = false;
    multisample_control.alpha_to_coverage = false;
    multisample_control.alpha_to_one = false;
    cull.enabled = false;
    cull.mode = GL_BACK;
    cull.front_face = GL_CCW;

    depth.test_enabled = false;
    depth.test_func = GL_LESS;
    depth.write_mask = GL_TRUE;

    primitive_restart.enabled = false;
    primitive_restart.index = 0;
    for (auto& item : color_mask) {
        item.red_enabled = GL_TRUE;
        item.green_enabled = GL_TRUE;
        item.blue_enabled = GL_TRUE;
        item.alpha_enabled = GL_TRUE;
    }
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
    for (auto& item : viewports) {
        item.x = 0;
        item.y = 0;
        item.width = 0;
        item.height = 0;
        item.depth_range_near = 0.0f;
        item.depth_range_far = 1.0f;
        item.scissor.enabled = false;
        item.scissor.x = 0;
        item.scissor.y = 0;
        item.scissor.width = 0;
        item.scissor.height = 0;
    }
    for (auto& item : blend) {
        item.enabled = true;
        item.rgb_equation = GL_FUNC_ADD;
        item.a_equation = GL_FUNC_ADD;
        item.src_rgb_func = GL_ONE;
        item.dst_rgb_func = GL_ZERO;
        item.src_a_func = GL_ONE;
        item.dst_a_func = GL_ZERO;
    }
    independant_blend.enabled = false;
    blend_color.red = 0.0f;
    blend_color.green = 0.0f;
    blend_color.blue = 0.0f;
    blend_color.alpha = 0.0f;
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

    clip_distance = {};

    point.size = 1;
    fragment_color_clamp.enabled = false;
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

void OpenGLState::ApplyColorMask() const {
    if (GLAD_GL_ARB_viewport_array && independant_blend.enabled) {
        for (size_t i = 0; i < Tegra::Engines::Maxwell3D::Regs::NumRenderTargets; i++) {
            const auto& updated = color_mask[i];
            const auto& current = cur_state.color_mask[i];
            if (updated.red_enabled != current.red_enabled ||
                updated.green_enabled != current.green_enabled ||
                updated.blue_enabled != current.blue_enabled ||
                updated.alpha_enabled != current.alpha_enabled) {
                glColorMaski(static_cast<GLuint>(i), updated.red_enabled, updated.green_enabled,
                             updated.blue_enabled, updated.alpha_enabled);
            }
        }
    } else {
        const auto& updated = color_mask[0];
        const auto& current = cur_state.color_mask[0];
        if (updated.red_enabled != current.red_enabled ||
            updated.green_enabled != current.green_enabled ||
            updated.blue_enabled != current.blue_enabled ||
            updated.alpha_enabled != current.alpha_enabled) {
            glColorMask(updated.red_enabled, updated.green_enabled, updated.blue_enabled,
                        updated.alpha_enabled);
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
// Viewport does not affects glClearBuffer so emulate viewport using scissor test
void OpenGLState::EmulateViewportWithScissor() {
    auto& current = viewports[0];
    if (current.scissor.enabled) {
        const GLint left = std::max(current.x, current.scissor.x);
        const GLint right =
            std::max(current.x + current.width, current.scissor.x + current.scissor.width);
        const GLint bottom = std::max(current.y, current.scissor.y);
        const GLint top =
            std::max(current.y + current.height, current.scissor.y + current.scissor.height);
        current.scissor.x = std::max(left, 0);
        current.scissor.y = std::max(bottom, 0);
        current.scissor.width = std::max(right - left, 0);
        current.scissor.height = std::max(top - bottom, 0);
    } else {
        current.scissor.enabled = true;
        current.scissor.x = current.x;
        current.scissor.y = current.y;
        current.scissor.width = current.width;
        current.scissor.height = current.height;
    }
}

void OpenGLState::ApplyViewport() const {
    if (GLAD_GL_ARB_viewport_array && geometry_shaders.enabled) {
        for (GLuint i = 0; i < static_cast<GLuint>(Tegra::Engines::Maxwell3D::Regs::NumViewports);
             i++) {
            const auto& current = cur_state.viewports[i];
            const auto& updated = viewports[i];
            if (updated.x != current.x || updated.y != current.y ||
                updated.width != current.width || updated.height != current.height) {
                glViewportIndexedf(
                    i, static_cast<GLfloat>(updated.x), static_cast<GLfloat>(updated.y),
                    static_cast<GLfloat>(updated.width), static_cast<GLfloat>(updated.height));
            }
            if (updated.depth_range_near != current.depth_range_near ||
                updated.depth_range_far != current.depth_range_far) {
                glDepthRangeIndexed(i, updated.depth_range_near, updated.depth_range_far);
            }
            const bool scissor_changed = updated.scissor.enabled != current.scissor.enabled;
            if (scissor_changed) {
                if (updated.scissor.enabled) {
                    glEnablei(GL_SCISSOR_TEST, i);
                } else {
                    glDisablei(GL_SCISSOR_TEST, i);
                }
            }
            if (updated.scissor.enabled &&
                (scissor_changed || updated.scissor.x != current.scissor.x ||
                 updated.scissor.y != current.scissor.y ||
                 updated.scissor.width != current.scissor.width ||
                 updated.scissor.height != current.scissor.height)) {
                glScissorIndexed(i, updated.scissor.x, updated.scissor.y, updated.scissor.width,
                                 updated.scissor.height);
            }
        }
    } else {
        const auto& current = cur_state.viewports[0];
        const auto& updated = viewports[0];
        if (updated.x != current.x || updated.y != current.y || updated.width != current.width ||
            updated.height != current.height) {
            glViewport(updated.x, updated.y, updated.width, updated.height);
        }
        if (updated.depth_range_near != current.depth_range_near ||
            updated.depth_range_far != current.depth_range_far) {
            glDepthRange(updated.depth_range_near, updated.depth_range_far);
        }
        const bool scissor_changed = updated.scissor.enabled != current.scissor.enabled;
        if (scissor_changed) {
            if (updated.scissor.enabled) {
                glEnable(GL_SCISSOR_TEST);
            } else {
                glDisable(GL_SCISSOR_TEST);
            }
        }
        if (updated.scissor.enabled && (scissor_changed || updated.scissor.x != current.scissor.x ||
                                        updated.scissor.y != current.scissor.y ||
                                        updated.scissor.width != current.scissor.width ||
                                        updated.scissor.height != current.scissor.height)) {
            glScissor(updated.scissor.x, updated.scissor.y, updated.scissor.width,
                      updated.scissor.height);
        }
    }
}

void OpenGLState::ApplyGlobalBlending() const {
    const Blend& current = cur_state.blend[0];
    const Blend& updated = blend[0];
    const bool blend_changed = updated.enabled != current.enabled;
    if (blend_changed) {
        if (updated.enabled) {
            glEnable(GL_BLEND);
        } else {
            glDisable(GL_BLEND);
        }
    }
    if (!updated.enabled) {
        return;
    }
    if (blend_changed || updated.src_rgb_func != current.src_rgb_func ||
        updated.dst_rgb_func != current.dst_rgb_func || updated.src_a_func != current.src_a_func ||
        updated.dst_a_func != current.dst_a_func) {
        glBlendFuncSeparate(updated.src_rgb_func, updated.dst_rgb_func, updated.src_a_func,
                            updated.dst_a_func);
    }

    if (blend_changed || updated.rgb_equation != current.rgb_equation ||
        updated.a_equation != current.a_equation) {
        glBlendEquationSeparate(updated.rgb_equation, updated.a_equation);
    }
}

void OpenGLState::ApplyTargetBlending(std::size_t target, bool force) const {
    const Blend& updated = blend[target];
    const Blend& current = cur_state.blend[target];
    const bool blend_changed = updated.enabled != current.enabled || force;
    if (blend_changed) {
        if (updated.enabled) {
            glEnablei(GL_BLEND, static_cast<GLuint>(target));
        } else {
            glDisablei(GL_BLEND, static_cast<GLuint>(target));
        }
    }
    if (!updated.enabled) {
        return;
    }
    if (blend_changed || updated.src_rgb_func != current.src_rgb_func ||
        updated.dst_rgb_func != current.dst_rgb_func || updated.src_a_func != current.src_a_func ||
        updated.dst_a_func != current.dst_a_func) {
        glBlendFuncSeparateiARB(static_cast<GLuint>(target), updated.src_rgb_func,
                                updated.dst_rgb_func, updated.src_a_func, updated.dst_a_func);
    }

    if (blend_changed || updated.rgb_equation != current.rgb_equation ||
        updated.a_equation != current.a_equation) {
        glBlendEquationSeparateiARB(static_cast<GLuint>(target), updated.rgb_equation,
                                    updated.a_equation);
    }
}

void OpenGLState::ApplyBlending() const {
    if (independant_blend.enabled) {
        for (size_t i = 0; i < Tegra::Engines::Maxwell3D::Regs::NumRenderTargets; i++) {
            ApplyTargetBlending(i,
                                independant_blend.enabled != cur_state.independant_blend.enabled);
        }
    } else {
        ApplyGlobalBlending();
    }
    if (blend_color.red != cur_state.blend_color.red ||
        blend_color.green != cur_state.blend_color.green ||
        blend_color.blue != cur_state.blend_color.blue ||
        blend_color.alpha != cur_state.blend_color.alpha) {
        glBlendColor(blend_color.red, blend_color.green, blend_color.blue, blend_color.alpha);
    }
}

void OpenGLState::ApplyLogicOp() const {
    const bool logic_op_changed = logic_op.enabled != cur_state.logic_op.enabled;
    if (logic_op_changed) {
        if (logic_op.enabled) {
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

void OpenGLState::ApplyFramebufferState() const {
    // Framebuffer
    if (draw.read_framebuffer != cur_state.draw.read_framebuffer) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, draw.read_framebuffer);
    }
    if (draw.draw_framebuffer != cur_state.draw.draw_framebuffer) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw.draw_framebuffer);
    }
}

void OpenGLState::ApplyVertexBufferState() const {
    // Vertex array
    if (draw.vertex_array != cur_state.draw.vertex_array) {
        glBindVertexArray(draw.vertex_array);
    }

    // Vertex buffer
    if (draw.vertex_buffer != cur_state.draw.vertex_buffer) {
        glBindBuffer(GL_ARRAY_BUFFER, draw.vertex_buffer);
    }
}

void OpenGLState::Apply() const {
    ApplyFramebufferState();
    ApplyVertexBufferState();
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
    // Point
    if (point.size != cur_state.point.size) {
        glPointSize(point.size);
    }
    if (GLAD_GL_ARB_color_buffer_float) {
        if (fragment_color_clamp.enabled != cur_state.fragment_color_clamp.enabled) {
            glClampColor(GL_CLAMP_FRAGMENT_COLOR_ARB,
                         fragment_color_clamp.enabled ? GL_TRUE : GL_FALSE);
        }
    }
    if (multisample_control.alpha_to_coverage != cur_state.multisample_control.alpha_to_coverage) {
        if (multisample_control.alpha_to_coverage) {
            glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        } else {
            glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        }
    }
    if (multisample_control.alpha_to_one != cur_state.multisample_control.alpha_to_one) {
        if (multisample_control.alpha_to_one) {
            glEnable(GL_SAMPLE_ALPHA_TO_ONE);
        } else {
            glDisable(GL_SAMPLE_ALPHA_TO_ONE);
        }
    }

    ApplyColorMask();
    ApplyViewport();
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
