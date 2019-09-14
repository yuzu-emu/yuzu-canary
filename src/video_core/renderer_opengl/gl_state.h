// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <glad/glad.h>
#include "video_core/engines/maxwell_3d.h"

namespace OpenGL {

namespace TextureUnits {

struct TextureUnit {
    GLint id;
    constexpr GLenum Enum() const {
        return static_cast<GLenum>(GL_TEXTURE0 + id);
    }
};

constexpr TextureUnit MaxwellTexture(int unit) {
    return TextureUnit{unit};
}

constexpr TextureUnit LightingLUT{3};
constexpr TextureUnit FogLUT{4};
constexpr TextureUnit ProcTexNoiseLUT{5};
constexpr TextureUnit ProcTexColorMap{6};
constexpr TextureUnit ProcTexAlphaMap{7};
constexpr TextureUnit ProcTexLUT{8};
constexpr TextureUnit ProcTexDiffLUT{9};

} // namespace TextureUnits

class OpenGLState {
public:
    struct {
        bool enabled; // GL_FRAMEBUFFER_SRGB
    } framebuffer_srgb;

    struct {
        bool alpha_to_coverage; // GL_ALPHA_TO_COVERAGE
        bool alpha_to_one;      // GL_ALPHA_TO_ONE
    } multisample_control;

    struct {
        bool enabled; // GL_CLAMP_FRAGMENT_COLOR_ARB
    } fragment_color_clamp;

    struct {
        bool far_plane;
        bool near_plane;
    } depth_clamp; // GL_DEPTH_CLAMP

    struct {
        bool enabled;      // GL_CULL_FACE
        GLenum mode;       // GL_CULL_FACE_MODE
        GLenum front_face; // GL_FRONT_FACE
    } cull;

    struct {
        bool test_enabled;    // GL_DEPTH_TEST
        GLenum test_func;     // GL_DEPTH_FUNC
        GLboolean write_mask; // GL_DEPTH_WRITEMASK
    } depth;

    struct {
        bool enabled;
        GLuint index;
    } primitive_restart; // GL_PRIMITIVE_RESTART

    struct ColorMask {
        GLboolean red_enabled;
        GLboolean green_enabled;
        GLboolean blue_enabled;
        GLboolean alpha_enabled;
    };
    std::array<ColorMask, Tegra::Engines::Maxwell3D::Regs::NumRenderTargets>
        color_mask; // GL_COLOR_WRITEMASK
    struct {
        bool test_enabled; // GL_STENCIL_TEST
        struct {
            GLenum test_func;           // GL_STENCIL_FUNC
            GLint test_ref;             // GL_STENCIL_REF
            GLuint test_mask;           // GL_STENCIL_VALUE_MASK
            GLuint write_mask;          // GL_STENCIL_WRITEMASK
            GLenum action_stencil_fail; // GL_STENCIL_FAIL
            GLenum action_depth_fail;   // GL_STENCIL_PASS_DEPTH_FAIL
            GLenum action_depth_pass;   // GL_STENCIL_PASS_DEPTH_PASS
        } front, back;
    } stencil;

    struct Blend {
        bool enabled;        // GL_BLEND
        GLenum rgb_equation; // GL_BLEND_EQUATION_RGB
        GLenum a_equation;   // GL_BLEND_EQUATION_ALPHA
        GLenum src_rgb_func; // GL_BLEND_SRC_RGB
        GLenum dst_rgb_func; // GL_BLEND_DST_RGB
        GLenum src_a_func;   // GL_BLEND_SRC_ALPHA
        GLenum dst_a_func;   // GL_BLEND_DST_ALPHA
    };
    std::array<Blend, Tegra::Engines::Maxwell3D::Regs::NumRenderTargets> blend;

    struct {
        bool enabled;
    } independant_blend;

    struct {
        GLclampf red;
        GLclampf green;
        GLclampf blue;
        GLclampf alpha;
    } blend_color; // GL_BLEND_COLOR

    struct {
        bool enabled; // GL_LOGIC_OP_MODE
        GLenum operation;
    } logic_op;

    std::array<GLuint, Tegra::Engines::Maxwell3D::Regs::NumTextureSamplers> textures{};
    std::array<GLuint, Tegra::Engines::Maxwell3D::Regs::NumTextureSamplers> samplers{};
    std::array<GLuint, Tegra::Engines::Maxwell3D::Regs::NumImages> images{};

    struct {
        GLuint read_framebuffer; // GL_READ_FRAMEBUFFER_BINDING
        GLuint draw_framebuffer; // GL_DRAW_FRAMEBUFFER_BINDING
        GLuint vertex_array;     // GL_VERTEX_ARRAY_BINDING
        GLuint shader_program;   // GL_CURRENT_PROGRAM
        GLuint program_pipeline; // GL_PROGRAM_PIPELINE_BINDING
    } draw;

    struct viewport {
        GLint x;
        GLint y;
        GLint width;
        GLint height;
        GLfloat depth_range_near; // GL_DEPTH_RANGE
        GLfloat depth_range_far;  // GL_DEPTH_RANGE
        struct {
            bool enabled; // GL_SCISSOR_TEST
            GLint x;
            GLint y;
            GLsizei width;
            GLsizei height;
        } scissor;
    };
    std::array<viewport, Tegra::Engines::Maxwell3D::Regs::NumViewports> viewports;

    struct {
        float size; // GL_POINT_SIZE
    } point;

    struct {
        bool point_enable;
        bool line_enable;
        bool fill_enable;
        GLfloat units;
        GLfloat factor;
        GLfloat clamp;
    } polygon_offset;

    struct {
        bool enabled; // GL_ALPHA_TEST
        GLenum func;  // GL_ALPHA_TEST_FUNC
        GLfloat ref;  // GL_ALPHA_TEST_REF
    } alpha_test;

    std::array<bool, 8> clip_distance; // GL_CLIP_DISTANCE

    OpenGLState();

    /// Get the currently active OpenGL state
    static OpenGLState GetCurState() {
        return cur_state;
    }

    void SetDefaultViewports();
    /// Apply this state as the current OpenGL state
    void Apply();

    void ApplyFramebufferState() const;
    void ApplyVertexArrayState() const;
    void ApplyShaderProgram() const;
    void ApplyProgramPipeline() const;
    void ApplyClipDistances() const;
    void ApplyPointSize() const;
    void ApplyFragmentColorClamp() const;
    void ApplyMultisample() const;
    void ApplySRgb() const;
    void ApplyCulling() const;
    void ApplyColorMask() const;
    void ApplyDepth() const;
    void ApplyPrimitiveRestart() const;
    void ApplyStencilTest() const;
    void ApplyViewport() const;
    void ApplyTargetBlending(std::size_t target, bool force) const;
    void ApplyGlobalBlending() const;
    void ApplyBlending() const;
    void ApplyLogicOp() const;
    void ApplyTextures() const;
    void ApplySamplers() const;
    void ApplyImages() const;
    void ApplyDepthClamp() const;
    void ApplyPolygonOffset() const;
    void ApplyAlphaTest() const;

    /// Set the initial OpenGL state
    static void ApplyDefaultState();

    /// Resets any references to the given resource
    OpenGLState& UnbindTexture(GLuint handle);
    OpenGLState& ResetSampler(GLuint handle);
    OpenGLState& ResetProgram(GLuint handle);
    OpenGLState& ResetPipeline(GLuint handle);
    OpenGLState& ResetVertexArray(GLuint handle);
    OpenGLState& ResetFramebuffer(GLuint handle);

    /// Viewport does not affects glClearBuffer so emulate viewport using scissor test
    void EmulateViewportWithScissor();

    void MarkDirtyBlendState() {
        dirty.blend_state = true;
    }

    void MarkDirtyStencilState() {
        dirty.stencil_state = true;
    }

    void MarkDirtyPolygonOffset() {
        dirty.polygon_offset = true;
    }

    void MarkDirtyColorMask() {
        dirty.color_mask = true;
    }

    void AllDirty() {
        dirty.blend_state = true;
        dirty.stencil_state = true;
        dirty.polygon_offset = true;
        dirty.color_mask = true;
    }

private:
    static OpenGLState cur_state;

    struct {
        bool blend_state;
        bool stencil_state;
        bool viewport_state;
        bool polygon_offset;
        bool color_mask;
    } dirty{};
};

} // namespace OpenGL
