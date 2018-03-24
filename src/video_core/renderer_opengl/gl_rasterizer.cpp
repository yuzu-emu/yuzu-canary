// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <glad/glad.h>
#include "common/alignment.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "common/math_util.h"
#include "common/microprofile.h"
#include "common/scope_exit.h"
#include "common/vector_math.h"
#include "core/settings.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/renderer_opengl/gl_shader_gen.h"
#include "video_core/renderer_opengl/renderer_opengl.h"

using PixelFormat = SurfaceParams::PixelFormat;
using SurfaceType = SurfaceParams::SurfaceType;

MICROPROFILE_DEFINE(OpenGL_VAO, "OpenGL", "Vertex Array Setup", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_VS, "OpenGL", "Vertex Shader Setup", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_FS, "OpenGL", "Fragment Shader Setup", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_Drawing, "OpenGL", "Drawing", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_Blits, "OpenGL", "Blits", MP_RGB(100, 100, 255));
MICROPROFILE_DEFINE(OpenGL_CacheManagement, "OpenGL", "Cache Mgmt", MP_RGB(100, 255, 100));

enum class UniformBindings : GLuint { Common, VS, FS };

static void SetShaderUniformBlockBinding(GLuint shader, const char* name, UniformBindings binding,
                                         size_t expected_size) {
    GLuint ub_index = glGetUniformBlockIndex(shader, name);
    if (ub_index != GL_INVALID_INDEX) {
        GLint ub_size = 0;
        glGetActiveUniformBlockiv(shader, ub_index, GL_UNIFORM_BLOCK_DATA_SIZE, &ub_size);
        ASSERT_MSG(ub_size == expected_size,
                   "Uniform block size did not match! Got %d, expected %zu",
                   static_cast<int>(ub_size), expected_size);
        glUniformBlockBinding(shader, ub_index, static_cast<GLuint>(binding));
    }
}

static void SetShaderUniformBlockBindings(GLuint shader) {
    SetShaderUniformBlockBinding(shader, "shader_data", UniformBindings::Common,
                                 sizeof(RasterizerOpenGL::UniformData));
    SetShaderUniformBlockBinding(shader, "vs_config", UniformBindings::VS,
                                 sizeof(RasterizerOpenGL::VSUniformData));
    SetShaderUniformBlockBinding(shader, "fs_config", UniformBindings::FS,
                                 sizeof(RasterizerOpenGL::FSUniformData));
}

RasterizerOpenGL::RasterizerOpenGL() {
    shader_dirty = true;

    has_ARB_buffer_storage = false;
    has_ARB_direct_state_access = false;
    has_ARB_separate_shader_objects = false;
    has_ARB_vertex_attrib_binding = false;

    GLint ext_num;
    glGetIntegerv(GL_NUM_EXTENSIONS, &ext_num);
    for (GLint i = 0; i < ext_num; i++) {
        std::string extension{reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i))};

        if (extension == "GL_ARB_buffer_storage") {
            has_ARB_buffer_storage = true;
        } else if (extension == "GL_ARB_direct_state_access") {
            has_ARB_direct_state_access = true;
        } else if (extension == "GL_ARB_separate_shader_objects") {
            has_ARB_separate_shader_objects = true;
        } else if (extension == "GL_ARB_vertex_attrib_binding") {
            has_ARB_vertex_attrib_binding = true;
        }
    }

    // Clipping plane 0 is always enabled for PICA fixed clip plane z <= 0
    state.clip_distance[0] = true;

    // Generate VBO, VAO and UBO
    vertex_buffer = OGLStreamBuffer::MakeBuffer(GLAD_GL_ARB_buffer_storage, GL_ARRAY_BUFFER);
    vertex_buffer->Create(VERTEX_BUFFER_SIZE, VERTEX_BUFFER_SIZE / 2);
    sw_vao.Create();
    uniform_buffer.Create();

    state.draw.vertex_array = sw_vao.handle;
    state.draw.vertex_buffer = vertex_buffer->GetHandle();
    state.draw.uniform_buffer = uniform_buffer.handle;
    state.Apply();

    glBufferData(GL_UNIFORM_BUFFER, sizeof(UniformData), nullptr, GL_STATIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, uniform_buffer.handle);

    uniform_block_data.dirty = true;

    // Create render framebuffer
    framebuffer.Create();

    if (has_ARB_separate_shader_objects) {
        hw_vao.Create();
        hw_vao_enabled_attributes.fill(false);

        stream_buffer = OGLStreamBuffer::MakeBuffer(has_ARB_buffer_storage, GL_ARRAY_BUFFER);
        stream_buffer->Create(STREAM_BUFFER_SIZE, STREAM_BUFFER_SIZE / 2);
        state.draw.vertex_buffer = stream_buffer->GetHandle();

        pipeline.Create();
        state.draw.program_pipeline = pipeline.handle;
        state.draw.shader_program = 0;
        state.draw.vertex_array = hw_vao.handle;
        state.Apply();

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, stream_buffer->GetHandle());

        vs_uniform_buffer.Create();
        glBindBuffer(GL_UNIFORM_BUFFER, vs_uniform_buffer.handle);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(VSUniformData), nullptr, GL_STREAM_COPY);
        glBindBufferBase(GL_UNIFORM_BUFFER, 1, vs_uniform_buffer.handle);
    } else {
        ASSERT_MSG(false, "Unimplemented");
    }

    accelerate_draw = AccelDraw::Disabled;

    glEnable(GL_BLEND);

    // Sync fixed function OpenGL state
    SyncClipEnabled();
    SyncClipCoef();
    SyncCullMode();
    SyncBlendEnabled();
    SyncBlendFuncs();
    SyncBlendColor();
}

RasterizerOpenGL::~RasterizerOpenGL() {
    if (stream_buffer != nullptr) {
        state.draw.vertex_buffer = stream_buffer->GetHandle();
        state.Apply();
        stream_buffer->Release();
    }
}

static constexpr std::array<GLenum, 4> vs_attrib_types{
    GL_BYTE,          // VertexAttributeFormat::BYTE
    GL_UNSIGNED_BYTE, // VertexAttributeFormat::UBYTE
    GL_SHORT,         // VertexAttributeFormat::SHORT
    GL_FLOAT          // VertexAttributeFormat::FLOAT
};

void RasterizerOpenGL::AnalyzeVertexArray(bool is_indexed) {
    UNIMPLEMENTED();
}

void RasterizerOpenGL::SetupVertexArray(u8* array_ptr, GLintptr buffer_offset) {
    MICROPROFILE_SCOPE(OpenGL_VAO);
    UNIMPLEMENTED();
}

void RasterizerOpenGL::SetupVertexShader(VSUniformData* ub_ptr, GLintptr buffer_offset) {
    MICROPROFILE_SCOPE(OpenGL_VS);
    UNIMPLEMENTED();
}

void RasterizerOpenGL::SetupFragmentShader(FSUniformData* ub_ptr, GLintptr buffer_offset) {
    MICROPROFILE_SCOPE(OpenGL_FS);
    ASSERT_MSG(false, "Unimplemented");
}

bool RasterizerOpenGL::AccelerateDrawBatch(bool is_indexed) {
    if (!has_ARB_separate_shader_objects) {
        ASSERT_MSG(false, "Unimplemented");
        return false;
    }

    accelerate_draw = is_indexed ? AccelDraw::Indexed : AccelDraw::Arrays;
    DrawTriangles();

    return true;
}

void RasterizerOpenGL::DrawTriangles() {
    MICROPROFILE_SCOPE(OpenGL_Drawing);
    UNIMPLEMENTED();
}

void RasterizerOpenGL::NotifyMaxwellRegisterChanged(u32 id) {}

void RasterizerOpenGL::FlushAll() {
    MICROPROFILE_SCOPE(OpenGL_CacheManagement);
    res_cache.FlushAll();
}

void RasterizerOpenGL::FlushRegion(VAddr addr, u64 size) {
    MICROPROFILE_SCOPE(OpenGL_CacheManagement);
    res_cache.FlushRegion(addr, size);
}

void RasterizerOpenGL::InvalidateRegion(VAddr addr, u64 size) {
    MICROPROFILE_SCOPE(OpenGL_CacheManagement);
    res_cache.InvalidateRegion(addr, size, nullptr);
}

void RasterizerOpenGL::FlushAndInvalidateRegion(VAddr addr, u64 size) {
    MICROPROFILE_SCOPE(OpenGL_CacheManagement);
    res_cache.FlushRegion(addr, size);
    res_cache.InvalidateRegion(addr, size, nullptr);
}

bool RasterizerOpenGL::AccelerateDisplayTransfer(const void* config) {
    MICROPROFILE_SCOPE(OpenGL_Blits);
    ASSERT_MSG(false, "Unimplemented");
    return true;
}

bool RasterizerOpenGL::AccelerateTextureCopy(const void* config) {
    ASSERT_MSG(false, "Unimplemented");
    return true;
}

bool RasterizerOpenGL::AccelerateFill(const void* config) {
    ASSERT_MSG(false, "Unimplemented");
    return true;
}

bool RasterizerOpenGL::AccelerateDisplay(const Tegra::FramebufferConfig& framebuffer,
                                         VAddr framebuffer_addr, u32 pixel_stride,
                                         ScreenInfo& screen_info) {
    if (framebuffer_addr == 0) {
        return false;
    }
    MICROPROFILE_SCOPE(OpenGL_CacheManagement);

    SurfaceParams src_params;
    src_params.addr = framebuffer_addr;
    src_params.width = std::min(framebuffer.width, pixel_stride);
    src_params.height = framebuffer.height;
    src_params.stride = pixel_stride;
    src_params.is_tiled = false;
    src_params.pixel_format =
        SurfaceParams::PixelFormatFromGPUPixelFormat(framebuffer.pixel_format);
    src_params.UpdateParams();

    MathUtil::Rectangle<u32> src_rect;
    Surface src_surface;
    std::tie(src_surface, src_rect) =
        res_cache.GetSurfaceSubRect(src_params, ScaleMatch::Ignore, true);

    if (src_surface == nullptr) {
        return false;
    }

    u32 scaled_width = src_surface->GetScaledWidth();
    u32 scaled_height = src_surface->GetScaledHeight();

    screen_info.display_texcoords = MathUtil::Rectangle<float>(
        (float)src_rect.bottom / (float)scaled_height, (float)src_rect.left / (float)scaled_width,
        (float)src_rect.top / (float)scaled_height, (float)src_rect.right / (float)scaled_width);

    screen_info.display_texture = src_surface->texture.handle;

    return true;
}

void RasterizerOpenGL::SetShader() {
    // TODO(bunnei): The below sets up a static test shader for passing untransformed vertices to
    // OpenGL for rendering. This should be removed/replaced when we start emulating Maxwell
    // shaders.

    static constexpr char vertex_shader[] = R"(
#version 150 core

in vec2 vert_position;
in vec2 vert_tex_coord;
out vec2 frag_tex_coord;

void main() {
    // Multiply input position by the rotscale part of the matrix and then manually translate by
    // the last column. This is equivalent to using a full 3x3 matrix and expanding the vector
    // to `vec3(vert_position.xy, 1.0)`
    gl_Position = vec4(mat2(mat3x2(0.0015625f, 0.0, 0.0, -0.0027778, -1.0, 1.0)) * vert_position + mat3x2(0.0015625f, 0.0, 0.0, -0.0027778, -1.0, 1.0)[2], 0.0, 1.0);
    frag_tex_coord = vert_tex_coord;
}
)";

    static constexpr char fragment_shader[] = R"(
#version 150 core

in vec2 frag_tex_coord;
out vec4 color;

uniform sampler2D color_texture;

void main() {
    color = vec4(1.0, 0.0, 0.0, 1.0);
}
)";

    if (current_shader) {
        return;
    }

    LOG_ERROR(HW_GPU, "Emulated shaders are not supported! Using a passthrough shader.");

    current_shader = &test_shader;
    if (has_ARB_separate_shader_objects) {
        test_shader.shader.Create(vertex_shader, nullptr, fragment_shader, {}, true);
        glActiveShaderProgram(pipeline.handle, test_shader.shader.handle);
    } else {
        ASSERT_MSG(false, "Unimplemented");
    }

    state.draw.shader_program = test_shader.shader.handle;
    state.Apply();

    if (has_ARB_separate_shader_objects) {
        state.draw.shader_program = 0;
        state.Apply();
    }
}

void RasterizerOpenGL::SyncClipEnabled() {
    ASSERT_MSG(false, "Unimplemented");
}

void RasterizerOpenGL::SyncClipCoef() {
    ASSERT_MSG(false, "Unimplemented");
}

void RasterizerOpenGL::SyncCullMode() {
    ASSERT_MSG(false, "Unimplemented");
}

void RasterizerOpenGL::SyncDepthScale() {
    ASSERT_MSG(false, "Unimplemented");
}

void RasterizerOpenGL::SyncDepthOffset() {
    ASSERT_MSG(false, "Unimplemented");
}

void RasterizerOpenGL::SyncBlendEnabled() {
    ASSERT_MSG(false, "Unimplemented");
}

void RasterizerOpenGL::SyncBlendFuncs() {
    ASSERT_MSG(false, "Unimplemented");
}

void RasterizerOpenGL::SyncBlendColor() {
    ASSERT_MSG(false, "Unimplemented");
}
