// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <string>
#include <fmt/format.h>
#include <glad/glad.h>

#include "common/assert.h"
#include "common/common_types.h"
#include "common/scope_exit.h"
#include "video_core/renderer_opengl/utils.h"

namespace OpenGL {

VertexArrayPushBuffer::VertexArrayPushBuffer() = default;

VertexArrayPushBuffer::~VertexArrayPushBuffer() = default;

void VertexArrayPushBuffer::Setup(GLuint vao_) {
    vao = vao_;
    index_buffer = nullptr;
    vertex_buffers.clear();
}

void VertexArrayPushBuffer::SetIndexBuffer(const GLuint* buffer) {
    index_buffer = buffer;
}

void VertexArrayPushBuffer::SetVertexBuffer(GLuint binding_index, const GLuint* buffer,
                                            GLintptr offset, GLsizei stride) {
    vertex_buffers.push_back(Entry{binding_index, buffer, offset, stride});
}

void VertexArrayPushBuffer::Bind() {
    if (index_buffer) {
        glVertexArrayElementBuffer(vao, *index_buffer);
    }

    // TODO(Rodrigo): Find a way to ARB_multi_bind this
    for (const auto& entry : vertex_buffers) {
        glVertexArrayVertexBuffer(vao, entry.binding_index, *entry.buffer, entry.offset,
                                  entry.stride);
    }
}

BindBuffersRangePushBuffer::BindBuffersRangePushBuffer(GLenum target) : target{target} {}

BindBuffersRangePushBuffer::~BindBuffersRangePushBuffer() = default;

void BindBuffersRangePushBuffer::Setup(GLuint first_) {
    first = first_;
    buffer_pointers.clear();
    offsets.clear();
    sizes.clear();
}

void BindBuffersRangePushBuffer::Push(const GLuint* buffer, GLintptr offset, GLsizeiptr size) {
    buffer_pointers.push_back(buffer);
    offsets.push_back(offset);
    sizes.push_back(size);
}

void BindBuffersRangePushBuffer::Bind() {
    // Ensure sizes are valid.
    const std::size_t count{buffer_pointers.size()};
    DEBUG_ASSERT(count == offsets.size() && count == sizes.size());
    if (count == 0) {
        return;
    }

    // Dereference buffers.
    buffers.resize(count);
    std::transform(buffer_pointers.begin(), buffer_pointers.end(), buffers.begin(),
                   [](const GLuint* pointer) { return *pointer; });

    glBindBuffersRange(target, first, static_cast<GLsizei>(count), buffers.data(), offsets.data(),
                       sizes.data());
}

void LabelGLObject(GLenum identifier, GLuint handle, VAddr addr, std::string_view extra_info) {
    if (!GLAD_GL_KHR_debug) {
        // We don't need to throw an error as this is just for debugging
        return;
    }

    std::string object_label;
    if (extra_info.empty()) {
        switch (identifier) {
        case GL_TEXTURE:
            object_label = fmt::format("Texture@0x{:016X}", addr);
            break;
        case GL_PROGRAM:
            object_label = fmt::format("Shader@0x{:016X}", addr);
            break;
        default:
            object_label = fmt::format("Object(0x{:X})@0x{:016X}", identifier, addr);
            break;
        }
    } else {
        object_label = fmt::format("{}@0x{:016X}", extra_info, addr);
    }
    glObjectLabel(identifier, handle, -1, static_cast<const GLchar*>(object_label.c_str()));
}

} // namespace OpenGL
