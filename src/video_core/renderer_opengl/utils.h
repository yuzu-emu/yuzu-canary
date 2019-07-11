// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string_view>
#include <vector>
#include <glad/glad.h>
#include "common/common_types.h"

namespace OpenGL {

class VertexArrayPushBuffer final {
public:
    explicit VertexArrayPushBuffer();
    ~VertexArrayPushBuffer();

    void Setup(GLuint vao_);

    void SetIndexBuffer(const GLuint* buffer);

    void SetVertexBuffer(GLuint binding_index, const GLuint* buffer, GLintptr offset,
                         GLsizei stride);

    void Bind();

private:
    struct Entry {
        GLuint binding_index{};
        const GLuint* buffer{};
        GLintptr offset{};
        GLsizei stride{};
    };

    GLuint vao{};
    const GLuint* index_buffer{};
    std::vector<Entry> vertex_buffers;
};

class BindBuffersRangePushBuffer final {
public:
    explicit BindBuffersRangePushBuffer(GLenum target);
    ~BindBuffersRangePushBuffer();

    void Setup(GLuint first_);

    void Push(const GLuint* buffer, GLintptr offset, GLsizeiptr size);

    void Bind();

private:
    GLenum target{};
    GLuint first{};
    std::vector<const GLuint*> buffer_pointers;

    std::vector<GLuint> buffers;
    std::vector<GLintptr> offsets;
    std::vector<GLsizeiptr> sizes;
};

void LabelGLObject(GLenum identifier, GLuint handle, VAddr addr, std::string_view extra_info = {});

} // namespace OpenGL
