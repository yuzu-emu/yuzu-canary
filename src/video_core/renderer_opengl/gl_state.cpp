// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <iterator>
#include <glad/glad.h>
#include "common/assert.h"
#include "common/logging/log.h"
#include "video_core/renderer_opengl/gl_state.h"

namespace OpenGL {

using Maxwell = Tegra::Engines::Maxwell3D::Regs;

OpenGLState OpenGLState::cur_state;
bool OpenGLState::s_rgb_used;

namespace {

template <typename T>
bool UpdateValue(T& current_value, const T new_value) {
    const bool changed = current_value != new_value;
    current_value = new_value;
    return changed;
}

template <typename T1, typename T2>
bool UpdateTie(T1 current_value, const T2 new_value) {
    const bool changed = current_value != new_value;
    current_value = new_value;
    return changed;
}

void Enable(GLenum cap, bool enable) {
    if (enable) {
        glEnable(cap);
    } else {
        glDisable(cap);
    }
}

void Enable(GLenum cap, GLuint index, bool enable) {
    if (enable) {
        glEnablei(cap, index);
    } else {
        glDisablei(cap, index);
    }
}

void Enable(GLenum cap, bool& current_value, bool new_value) {
    if (UpdateValue(current_value, new_value))
        Enable(cap, new_value);
}

void Enable(GLenum cap, GLuint index, bool& current_value, bool new_value) {
    if (UpdateValue(current_value, new_value))
        Enable(cap, index, new_value);
}

} // namespace

OpenGLState::OpenGLState() {
    // These all match default OpenGL values
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

    const auto ResetStencil = [](auto& config) {
        config.test_func = GL_ALWAYS;
        config.test_ref = 0;
        config.test_mask = 0xFFFFFFFF;
        config.write_mask = 0xFFFFFFFF;
        config.action_depth_fail = GL_KEEP;
        config.action_depth_pass = GL_KEEP;
        config.action_stencil_fail = GL_KEEP;
    };
    stencil.test_enabled = false;
    ResetStencil(stencil.front);
    ResetStencil(stencil.back);

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
    draw.shader_program = 0;
    draw.program_pipeline = 0;

    clip_distance = {};

    point.size = 1;

    fragment_color_clamp.enabled = false;

    depth_clamp.far_plane = false;
    depth_clamp.near_plane = false;

    polygon_offset.fill_enable = false;
    polygon_offset.line_enable = false;
    polygon_offset.point_enable = false;
    polygon_offset.factor = 0.0f;
    polygon_offset.units = 0.0f;
    polygon_offset.clamp = 0.0f;

    alpha_test.enabled = false;
    alpha_test.func = GL_ALWAYS;
    alpha_test.ref = 0.0f;
}

void OpenGLState::ApplyDefaultState() {
    glEnable(GL_BLEND);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_PRIMITIVE_RESTART);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_COLOR_LOGIC_OP);
    glDisable(GL_SCISSOR_TEST);
}

void OpenGLState::ApplyFramebufferState() const {
    if (UpdateValue(cur_state.draw.read_framebuffer, draw.read_framebuffer)) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, draw.read_framebuffer);
    }
    if (UpdateValue(cur_state.draw.draw_framebuffer, draw.draw_framebuffer)) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw.draw_framebuffer);
    }
}

void OpenGLState::ApplyVertexArrayState() const {
    if (UpdateValue(cur_state.draw.vertex_array, draw.vertex_array)) {
        glBindVertexArray(draw.vertex_array);
    }
}

void OpenGLState::ApplyShaderProgram() const {
    if (UpdateValue(cur_state.draw.shader_program, draw.shader_program)) {
        glUseProgram(draw.shader_program);
    }
}

void OpenGLState::ApplyProgramPipeline() const {
    if (UpdateValue(cur_state.draw.program_pipeline, draw.program_pipeline)) {
        glBindProgramPipeline(draw.program_pipeline);
    }
}

void OpenGLState::ApplyClipDistances() const {
    for (std::size_t i = 0; i < clip_distance.size(); ++i) {
        Enable(GL_CLIP_DISTANCE0 + static_cast<GLenum>(i), cur_state.clip_distance[i],
               clip_distance[i]);
    }
}

void OpenGLState::ApplyPointSize() const {
    if (UpdateValue(cur_state.point.size, point.size)) {
        glPointSize(point.size);
    }
}

void OpenGLState::ApplyFragmentColorClamp() const {
    if (UpdateValue(cur_state.fragment_color_clamp.enabled, fragment_color_clamp.enabled)) {
        glClampColor(GL_CLAMP_FRAGMENT_COLOR_ARB,
                     fragment_color_clamp.enabled ? GL_TRUE : GL_FALSE);
    }
}

void OpenGLState::ApplyMultisample() const {
    Enable(GL_SAMPLE_ALPHA_TO_COVERAGE, cur_state.multisample_control.alpha_to_coverage,
           multisample_control.alpha_to_coverage);
    Enable(GL_SAMPLE_ALPHA_TO_ONE, cur_state.multisample_control.alpha_to_one,
           multisample_control.alpha_to_one);
}

void OpenGLState::ApplyDepthClamp() const {
    if (depth_clamp.far_plane == cur_state.depth_clamp.far_plane &&
        depth_clamp.near_plane == cur_state.depth_clamp.near_plane) {
        return;
    }
    cur_state.depth_clamp = depth_clamp;

    UNIMPLEMENTED_IF_MSG(depth_clamp.far_plane != depth_clamp.near_plane,
                         "Unimplemented Depth Clamp Separation!");

    Enable(GL_DEPTH_CLAMP, depth_clamp.far_plane || depth_clamp.near_plane);
}

void OpenGLState::ApplySRgb() const {
    if (cur_state.framebuffer_srgb.enabled == framebuffer_srgb.enabled)
        return;
    cur_state.framebuffer_srgb.enabled = framebuffer_srgb.enabled;
    if (framebuffer_srgb.enabled) {
        // Track if sRGB is used
        s_rgb_used = true;
        glEnable(GL_FRAMEBUFFER_SRGB);
    } else {
        glDisable(GL_FRAMEBUFFER_SRGB);
    }
}

void OpenGLState::ApplyCulling() const {
    Enable(GL_CULL_FACE, cur_state.cull.enabled, cull.enabled);

    if (UpdateValue(cur_state.cull.mode, cull.mode)) {
        glCullFace(cull.mode);
    }

    if (UpdateValue(cur_state.cull.front_face, cull.front_face)) {
        glFrontFace(cull.front_face);
    }
}

void OpenGLState::ApplyColorMask() const {
    for (std::size_t i = 0; i < Maxwell::NumRenderTargets; ++i) {
        const auto& updated = color_mask[i];
        auto& current = cur_state.color_mask[i];
        if (updated.red_enabled != current.red_enabled ||
            updated.green_enabled != current.green_enabled ||
            updated.blue_enabled != current.blue_enabled ||
            updated.alpha_enabled != current.alpha_enabled) {
            current = updated;
            glColorMaski(static_cast<GLuint>(i), updated.red_enabled, updated.green_enabled,
                         updated.blue_enabled, updated.alpha_enabled);
        }
    }
}

void OpenGLState::ApplyDepth() const {
    Enable(GL_DEPTH_TEST, cur_state.depth.test_enabled, depth.test_enabled);

    if (cur_state.depth.test_func != depth.test_func) {
        cur_state.depth.test_func = depth.test_func;
        glDepthFunc(depth.test_func);
    }

    if (cur_state.depth.write_mask != depth.write_mask) {
        cur_state.depth.write_mask = depth.write_mask;
        glDepthMask(depth.write_mask);
    }
}

void OpenGLState::ApplyPrimitiveRestart() const {
    Enable(GL_PRIMITIVE_RESTART, cur_state.primitive_restart.enabled, primitive_restart.enabled);

    if (cur_state.primitive_restart.index != primitive_restart.index) {
        cur_state.primitive_restart.index = primitive_restart.index;
        glPrimitiveRestartIndex(primitive_restart.index);
    }
}

void OpenGLState::ApplyStencilTest() const {
    Enable(GL_STENCIL_TEST, cur_state.stencil.test_enabled, stencil.test_enabled);

    const auto ConfigStencil = [](GLenum face, const auto& config, auto& current) {
        if (current.test_func != config.test_func || current.test_ref != config.test_ref ||
            current.test_mask != config.test_mask) {
            current.test_func = config.test_func;
            current.test_ref = config.test_ref;
            current.test_mask = config.test_mask;
            glStencilFuncSeparate(face, config.test_func, config.test_ref, config.test_mask);
        }
        if (current.action_depth_fail != config.action_depth_fail ||
            current.action_depth_pass != config.action_depth_pass ||
            current.action_stencil_fail != config.action_stencil_fail) {
            current.action_depth_fail = config.action_depth_fail;
            current.action_depth_pass = config.action_depth_pass;
            current.action_stencil_fail = config.action_stencil_fail;
            glStencilOpSeparate(face, config.action_stencil_fail, config.action_depth_fail,
                                config.action_depth_pass);
        }
        if (current.write_mask != config.write_mask) {
            current.write_mask = config.write_mask;
            glStencilMaskSeparate(face, config.write_mask);
        }
    };
    ConfigStencil(GL_FRONT, stencil.front, cur_state.stencil.front);
    ConfigStencil(GL_BACK, stencil.back, cur_state.stencil.back);
}

void OpenGLState::ApplyViewport() const {
    for (GLuint i = 0; i < static_cast<GLuint>(Maxwell::NumViewports); ++i) {
        const auto& updated = viewports[i];
        auto& current = cur_state.viewports[i];

        if (current.x != updated.x || current.y != updated.y || current.width != updated.width ||
            current.height != updated.height) {
            current.x = updated.x;
            current.y = updated.y;
            current.width = updated.width;
            current.height = updated.height;
            glViewportIndexedf(i, static_cast<GLfloat>(updated.x), static_cast<GLfloat>(updated.y),
                               static_cast<GLfloat>(updated.width),
                               static_cast<GLfloat>(updated.height));
        }
        if (current.depth_range_near != updated.depth_range_near ||
            current.depth_range_far != updated.depth_range_far) {
            current.depth_range_near = updated.depth_range_near;
            current.depth_range_far = updated.depth_range_far;
            glDepthRangeIndexed(i, updated.depth_range_near, updated.depth_range_far);
        }

        Enable(GL_SCISSOR_TEST, i, current.scissor.enabled, updated.scissor.enabled);

        if (current.scissor.x != updated.scissor.x || current.scissor.y != updated.scissor.y ||
            current.scissor.width != updated.scissor.width ||
            current.scissor.height != updated.scissor.height) {
            current.scissor.x = updated.scissor.x;
            current.scissor.y = updated.scissor.y;
            current.scissor.width = updated.scissor.width;
            current.scissor.height = updated.scissor.height;
            glScissorIndexed(i, updated.scissor.x, updated.scissor.y, updated.scissor.width,
                             updated.scissor.height);
        }
    }
}

void OpenGLState::ApplyGlobalBlending() const {
    const Blend& updated = blend[0];
    Blend& current = cur_state.blend[0];

    Enable(GL_BLEND, current.enabled, updated.enabled);

    if (current.src_rgb_func != updated.src_rgb_func ||
        current.dst_rgb_func != updated.dst_rgb_func || current.src_a_func != updated.src_a_func ||
        current.dst_a_func != updated.dst_a_func) {
        current.src_rgb_func = updated.src_rgb_func;
        current.dst_rgb_func = updated.dst_rgb_func;
        current.src_a_func = updated.src_a_func;
        current.dst_a_func = updated.dst_a_func;
        glBlendFuncSeparate(updated.src_rgb_func, updated.dst_rgb_func, updated.src_a_func,
                            updated.dst_a_func);
    }

    if (current.rgb_equation != updated.rgb_equation || current.a_equation != updated.a_equation) {
        current.rgb_equation = updated.rgb_equation;
        current.a_equation = updated.a_equation;
        glBlendEquationSeparate(updated.rgb_equation, updated.a_equation);
    }
}

void OpenGLState::ApplyTargetBlending(std::size_t target, bool force) const {
    const Blend& updated = blend[target];
    Blend& current = cur_state.blend[target];

    if (current.enabled != updated.enabled || force) {
        current.enabled = updated.enabled;
        Enable(GL_BLEND, static_cast<GLuint>(target), updated.enabled);
    }

    if (UpdateTie(std::tie(current.src_rgb_func, current.dst_rgb_func, current.src_a_func,
                           current.dst_a_func),
                  std::tie(updated.src_rgb_func, updated.dst_rgb_func, updated.src_a_func,
                           updated.dst_a_func))) {
        glBlendFuncSeparatei(static_cast<GLuint>(target), updated.src_rgb_func,
                             updated.dst_rgb_func, updated.src_a_func, updated.dst_a_func);
    }

    if (UpdateTie(std::tie(current.rgb_equation, current.a_equation),
                  std::tie(updated.rgb_equation, updated.a_equation))) {
        glBlendEquationSeparatei(static_cast<GLuint>(target), updated.rgb_equation,
                                 updated.a_equation);
    }
}

void OpenGLState::ApplyBlending() const {
    if (independant_blend.enabled) {
        const bool force = independant_blend.enabled != cur_state.independant_blend.enabled;
        for (std::size_t target = 0; target < Maxwell::NumRenderTargets; ++target) {
            ApplyTargetBlending(target, force);
        }
    } else {
        ApplyGlobalBlending();
    }
    cur_state.independant_blend.enabled = independant_blend.enabled;

    if (UpdateTie(
            std::tie(cur_state.blend_color.red, cur_state.blend_color.green,
                     cur_state.blend_color.blue, cur_state.blend_color.alpha),
            std::tie(blend_color.red, blend_color.green, blend_color.blue, blend_color.alpha))) {
        glBlendColor(blend_color.red, blend_color.green, blend_color.blue, blend_color.alpha);
    }
}

void OpenGLState::ApplyLogicOp() const {
    Enable(GL_COLOR_LOGIC_OP, cur_state.logic_op.enabled, logic_op.enabled);

    if (UpdateValue(cur_state.logic_op.operation, logic_op.operation)) {
        glLogicOp(logic_op.operation);
    }
}

void OpenGLState::ApplyPolygonOffset() const {
    Enable(GL_POLYGON_OFFSET_FILL, cur_state.polygon_offset.fill_enable,
           polygon_offset.fill_enable);
    Enable(GL_POLYGON_OFFSET_LINE, cur_state.polygon_offset.line_enable,
           polygon_offset.line_enable);
    Enable(GL_POLYGON_OFFSET_POINT, cur_state.polygon_offset.point_enable,
           polygon_offset.point_enable);

    if (UpdateTie(std::tie(cur_state.polygon_offset.factor, cur_state.polygon_offset.units,
                           cur_state.polygon_offset.clamp),
                  std::tie(polygon_offset.factor, polygon_offset.units, polygon_offset.clamp))) {
        if (GLAD_GL_EXT_polygon_offset_clamp && polygon_offset.clamp != 0) {
            glPolygonOffsetClamp(polygon_offset.factor, polygon_offset.units, polygon_offset.clamp);
        } else {
            UNIMPLEMENTED_IF_MSG(polygon_offset.clamp != 0,
                                 "Unimplemented Depth polygon offset clamp.");
            glPolygonOffset(polygon_offset.factor, polygon_offset.units);
        }
    }
}

void OpenGLState::ApplyAlphaTest() const {
    Enable(GL_ALPHA_TEST, cur_state.alpha_test.enabled, alpha_test.enabled);
    if (UpdateTie(std::tie(cur_state.alpha_test.func, cur_state.alpha_test.ref),
                  std::tie(alpha_test.func, alpha_test.ref))) {
        glAlphaFunc(alpha_test.func, alpha_test.ref);
    }
}

void OpenGLState::ApplyTextures() const {
    bool has_delta{};
    std::size_t first{};
    std::size_t last{};
    std::array<GLuint, Maxwell::NumTextureSamplers> textures;

    for (std::size_t i = 0; i < std::size(texture_units); ++i) {
        const auto& texture_unit = texture_units[i];
        auto& cur_state_texture_unit = cur_state.texture_units[i];
        textures[i] = texture_unit.texture;
        if (cur_state_texture_unit.texture == textures[i]) {
            continue;
        }
        cur_state_texture_unit.texture = textures[i];
        if (!has_delta) {
            first = i;
            has_delta = true;
        }
        last = i;
    }
    if (has_delta) {
        glBindTextures(static_cast<GLuint>(first), static_cast<GLsizei>(last - first + 1),
                       textures.data() + first);
    }
}

void OpenGLState::ApplySamplers() const {
    bool has_delta{};
    std::size_t first{};
    std::size_t last{};
    std::array<GLuint, Maxwell::NumTextureSamplers> samplers;

    for (std::size_t i = 0; i < std::size(samplers); ++i) {
        samplers[i] = texture_units[i].sampler;
        if (cur_state.texture_units[i].sampler == texture_units[i].sampler) {
            continue;
        }
        cur_state.texture_units[i].sampler = texture_units[i].sampler;
        if (!has_delta) {
            first = i;
            has_delta = true;
        }
        last = i;
    }
    if (has_delta) {
        glBindSamplers(static_cast<GLuint>(first), static_cast<GLsizei>(last - first + 1),
                       samplers.data() + first);
    }
}

void OpenGLState::Apply() const {
    ApplyFramebufferState();
    ApplyVertexArrayState();
    ApplyShaderProgram();
    ApplyProgramPipeline();
    ApplyClipDistances();
    ApplyPointSize();
    ApplyFragmentColorClamp();
    ApplyMultisample();
    ApplyDepthClamp();
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
    ApplyPolygonOffset();
    ApplyAlphaTest();
}

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
