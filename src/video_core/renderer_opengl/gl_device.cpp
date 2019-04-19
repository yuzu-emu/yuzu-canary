// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstddef>
#include <glad/glad.h>

#include "common/logging/log.h"
#include "video_core/renderer_opengl/gl_device.h"

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
    has_variable_aoffi = TestVariableAoffi();
}

bool Device::TestVariableAoffi() {
    const GLchar* AOFFI_TEST = R"(#version 430 core
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

} // namespace OpenGL
