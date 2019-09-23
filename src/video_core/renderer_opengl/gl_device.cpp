// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>
#include <glad/glad.h>

#include "common/logging/log.h"
#include "common/scope_exit.h"
#include "video_core/renderer_opengl/gl_device.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"

namespace OpenGL {

namespace {

template <typename T>
T GetInteger(GLenum pname) {
    GLint temporary;
    glGetIntegerv(pname, &temporary);
    return static_cast<T>(temporary);
}

bool TestProgram(const GLchar* glsl) {
    const GLuint shader{glCreateShaderProgramv(GL_VERTEX_SHADER, 1, &glsl)};
    GLint link_status;
    glGetProgramiv(shader, GL_LINK_STATUS, &link_status);
    glDeleteProgram(shader);
    return link_status == GL_TRUE;
}

std::vector<std::string_view> GetExtensions() {
    GLint num_extensions;
    glGetIntegerv(GL_NUM_EXTENSIONS, &num_extensions);
    std::vector<std::string_view> extensions;
    extensions.reserve(num_extensions);
    for (GLint index = 0; index < num_extensions; ++index) {
        extensions.push_back(
            reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(index))));
    }
    return extensions;
}

bool HasExtension(const std::vector<std::string_view>& images, std::string_view extension) {
    return std::find(images.begin(), images.end(), extension) != images.end();
}

} // Anonymous namespace

Device::Device() {
    const std::vector extensions = GetExtensions();

    uniform_buffer_alignment = GetInteger<std::size_t>(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT);
    shader_storage_alignment = GetInteger<std::size_t>(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT);
    max_vertex_attributes = GetInteger<u32>(GL_MAX_VERTEX_ATTRIBS);
    max_varyings = GetInteger<u32>(GL_MAX_VARYING_VECTORS);
    has_warp_intrinsics = GLAD_GL_NV_gpu_shader5 && GLAD_GL_NV_shader_thread_group &&
                          GLAD_GL_NV_shader_thread_shuffle;
    has_vertex_viewport_layer = GLAD_GL_ARB_shader_viewport_layer_array;
    has_image_load_formatted = HasExtension(extensions, "GL_EXT_shader_image_load_formatted");
    has_variable_aoffi = TestVariableAoffi();
    has_component_indexing_bug = TestComponentIndexingBug();
    has_precise_bug = TestPreciseBug();

    LOG_INFO(Render_OpenGL, "Renderer_VariableAOFFI: {}", has_variable_aoffi);
    LOG_INFO(Render_OpenGL, "Renderer_ComponentIndexingBug: {}", has_component_indexing_bug);
    LOG_INFO(Render_OpenGL, "Renderer_PreciseBug: {}", has_precise_bug);
}

Device::Device(std::nullptr_t) {
    uniform_buffer_alignment = 0;
    max_vertex_attributes = 16;
    max_varyings = 15;
    has_warp_intrinsics = true;
    has_vertex_viewport_layer = true;
    has_image_load_formatted = true;
    has_variable_aoffi = true;
    has_component_indexing_bug = false;
    has_precise_bug = false;
}

bool Device::TestVariableAoffi() {
    return TestProgram(R"(#version 430 core
// This is a unit test, please ignore me on apitrace bug reports.
uniform sampler2D tex;
uniform ivec2 variable_offset;
out vec4 output_attribute;
void main() {
    output_attribute = textureOffset(tex, vec2(0), variable_offset);
})");
}

bool Device::TestComponentIndexingBug() {
    const GLchar* COMPONENT_TEST = R"(#version 430 core
layout (std430, binding = 0) buffer OutputBuffer {
    uint output_value;
};
layout (std140, binding = 0) uniform InputBuffer {
    uvec4 input_value[4096];
};
layout (location = 0) uniform uint idx;
void main() {
    output_value = input_value[idx >> 2][idx & 3];
})";
    const GLuint shader{glCreateShaderProgramv(GL_VERTEX_SHADER, 1, &COMPONENT_TEST)};
    SCOPE_EXIT({ glDeleteProgram(shader); });
    glUseProgram(shader);

    OGLVertexArray vao;
    vao.Create();
    glBindVertexArray(vao.handle);

    constexpr std::array<GLuint, 8> values{0, 0, 0, 0, 0x1236327, 0x985482, 0x872753, 0x2378432};
    OGLBuffer ubo;
    ubo.Create();
    glNamedBufferData(ubo.handle, sizeof(values), values.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo.handle);

    OGLBuffer ssbo;
    ssbo.Create();
    glNamedBufferStorage(ssbo.handle, sizeof(GLuint), nullptr, GL_CLIENT_STORAGE_BIT);

    for (GLuint index = 4; index < 8; ++index) {
        glInvalidateBufferData(ssbo.handle);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo.handle);

        glProgramUniform1ui(shader, 0, index);
        glDrawArrays(GL_POINTS, 0, 1);

        GLuint result;
        glGetNamedBufferSubData(ssbo.handle, 0, sizeof(result), &result);
        if (result != values.at(index)) {
            return true;
        }
    }
    return false;
}

bool Device::TestPreciseBug() {
    return !TestProgram(R"(#version 430 core
in vec3 coords;
out float out_value;
uniform sampler2DShadow tex;
void main() {
    precise float tmp_value = vec4(texture(tex, coords)).x;
    out_value = tmp_value;
})");
}

} // namespace OpenGL
