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
        bool enabled;      // GL_CULL_FACE
        GLenum mode;       // GL_CULL_FACE_MODE
        GLenum front_face; // GL_FRONT_FACE
    } cull;

    struct {
        bool test_enabled;        // GL_DEPTH_TEST
        GLenum test_func;         // GL_DEPTH_FUNC
        GLboolean write_mask;     // GL_DEPTH_WRITEMASK
        GLfloat depth_range_near; // GL_DEPTH_RANGE
        GLfloat depth_range_far;  // GL_DEPTH_RANGE
    } depth;

    struct {
        bool enabled;
        GLuint index;
    } primitive_restart; // GL_PRIMITIVE_RESTART

    struct {
        GLboolean red_enabled;
        GLboolean green_enabled;
        GLboolean blue_enabled;
        GLboolean alpha_enabled;
    } color_mask; // GL_COLOR_WRITEMASK

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

    struct {
        bool enabled;        // GL_BLEND
        GLenum rgb_equation; // GL_BLEND_EQUATION_RGB
        GLenum a_equation;   // GL_BLEND_EQUATION_ALPHA
        GLenum src_rgb_func; // GL_BLEND_SRC_RGB
        GLenum dst_rgb_func; // GL_BLEND_DST_RGB
        GLenum src_a_func;   // GL_BLEND_SRC_ALPHA
        GLenum dst_a_func;   // GL_BLEND_DST_ALPHA

        struct {
            GLclampf red;
            GLclampf green;
            GLclampf blue;
            GLclampf alpha;
        } color; // GL_BLEND_COLOR
    } blend;

    struct {
        bool enabled; // GL_LOGIC_OP_MODE
        GLenum operation;
    } logic_op;

    // 3 texture units - one for each that is used in PICA fragment shader emulation
    struct TextureUnit {
        GLuint texture; // GL_TEXTURE_BINDING_2D
        GLuint sampler; // GL_SAMPLER_BINDING
        GLenum target;
        struct {
            GLint r; // GL_TEXTURE_SWIZZLE_R
            GLint g; // GL_TEXTURE_SWIZZLE_G
            GLint b; // GL_TEXTURE_SWIZZLE_B
            GLint a; // GL_TEXTURE_SWIZZLE_A
        } swizzle;

        void Unbind() {
            texture = 0;
            swizzle.r = GL_RED;
            swizzle.g = GL_GREEN;
            swizzle.b = GL_BLUE;
            swizzle.a = GL_ALPHA;
        }

        void Reset() {
            Unbind();
            sampler = 0;
            target = GL_TEXTURE_2D;
        }
    };
    std::array<TextureUnit, Tegra::Engines::Maxwell3D::Regs::NumTextureSamplers> texture_units;

    struct {
        GLuint read_framebuffer; // GL_READ_FRAMEBUFFER_BINDING
        GLuint draw_framebuffer; // GL_DRAW_FRAMEBUFFER_BINDING
        GLuint vertex_array;     // GL_VERTEX_ARRAY_BINDING
        GLuint vertex_buffer;    // GL_ARRAY_BUFFER_BINDING
        GLuint uniform_buffer;   // GL_UNIFORM_BUFFER_BINDING
        GLuint shader_program;   // GL_CURRENT_PROGRAM
        GLuint program_pipeline; // GL_PROGRAM_PIPELINE_BINDING
    } draw;

    struct {
        bool enabled; // GL_SCISSOR_TEST
        GLint x;
        GLint y;
        GLsizei width;
        GLsizei height;
    } scissor;

    struct {
        GLint x;
        GLint y;
        GLsizei width;
        GLsizei height;
    } viewport;

    struct {
        float size; // GL_POINT_SIZE
    } point;

    std::array<bool, 2> clip_distance; // GL_CLIP_DISTANCE

    OpenGLState();

    /// Get the currently active OpenGL state
    static OpenGLState GetCurState() {
        return cur_state;
    }
    static bool GetsRGBUsed() {
        return s_rgb_used;
    }
    static void ClearsRGBUsed() {
        s_rgb_used = false;
    }
    /// Apply this state as the current OpenGL state
    void Apply() const;

    /// Resets any references to the given resource
    OpenGLState& UnbindTexture(GLuint handle);
    OpenGLState& ResetSampler(GLuint handle);
    OpenGLState& ResetProgram(GLuint handle);
    OpenGLState& ResetPipeline(GLuint handle);
    OpenGLState& ResetBuffer(GLuint handle);
    OpenGLState& ResetVertexArray(GLuint handle);
    OpenGLState& ResetFramebuffer(GLuint handle);

private:
    static OpenGLState cur_state;
    // Workaround for sRGB problems caused by
    // QT not supporting srgb output
    static bool s_rgb_used;
};

} // namespace OpenGL
