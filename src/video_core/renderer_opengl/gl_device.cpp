// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <cstddef>
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
} // Anonymous namespace

Device::Device() {
    uniform_buffer_alignment = GetInteger<std::size_t>(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT);
    max_vertex_attributes = GetInteger<u32>(GL_MAX_VERTEX_ATTRIBS);
    max_varyings = GetInteger<u32>(GL_MAX_VARYING_VECTORS);
    has_variable_aoffi = TestVariableAoffi();
    has_component_indexing_bug = TestComponentIndexingBug();
}

Device::Device(std::nullptr_t) {
    uniform_buffer_alignment = 0;
    max_vertex_attributes = 16;
    max_varyings = 15;
    has_variable_aoffi = true;
    has_component_indexing_bug = false;
}

bool Device::TestVariableAoffi() {
    const GLchar* AOFFI_TEST = R"(#version 430 core
// This is a unit test, please ignore me on apitrace bug reports.
uniform sampler2D tex;
uniform ivec2 variable_offset;
void main() {
    gl_Position = textureOffset(tex, vec2(0), variable_offset);
}
)";
    const GLuint shader{glCreateShaderProgramv(GL_VERTEX_SHADER, 1, &AOFFI_TEST)};
    GLint link_status{};
    glGetProgramiv(shader, GL_LINK_STATUS, &link_status);
    glDeleteProgram(shader);

    const bool supported{link_status == GL_TRUE};
    LOG_INFO(Render_OpenGL, "Renderer_VariableAOFFI: {}", supported);
    return supported;
}

bool Device::TestComponentIndexingBug() {
    constexpr char log_message[] = "Renderer_ComponentIndexingBug: {}";
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
            LOG_INFO(Render_OpenGL, log_message, true);
            return true;
        }
    }
    LOG_INFO(Render_OpenGL, log_message, false);
    return false;
}

} // namespace OpenGL
