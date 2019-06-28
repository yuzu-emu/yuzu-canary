// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <deque>
#include <vector>
#include "common/alignment.h"
#include "common/assert.h"
#include "common/microprofile.h"
#include "video_core/renderer_opengl/gl_state.h"
#include "video_core/renderer_opengl/gl_stream_buffer.h"

MICROPROFILE_DEFINE(OpenGL_StreamBuffer, "OpenGL", "Stream Buffer Orphaning",
                    MP_RGB(128, 128, 192));

namespace OpenGL {

OGLStreamBuffer::OGLStreamBuffer(GLsizeiptr size, bool vertex_data_usage, bool prefer_coherent,
                                 bool use_persistent)
    : buffer_size(size) {
    gl_buffer.Create();

    GLsizeiptr allocate_size = size;
    if (vertex_data_usage) {
        // On AMD GPU there is a strange crash in indexed drawing. The crash happens when the buffer
        // read position is near the end and is an out-of-bound access to the vertex buffer. This is
        // probably a bug in the driver and is related to the usage of vec3<byte> attributes in the
        // vertex array. Doubling the allocation size for the vertex buffer seems to avoid the
        // crash.
        allocate_size *= 2;
    }

    if (use_persistent) {
        persistent = true;
        coherent = prefer_coherent;
        const GLbitfield flags =
            GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | (coherent ? GL_MAP_COHERENT_BIT : 0);
        glNamedBufferStorage(gl_buffer.handle, allocate_size, nullptr, flags);
        mapped_ptr = static_cast<u8*>(glMapNamedBufferRange(
            gl_buffer.handle, 0, buffer_size, flags | (coherent ? 0 : GL_MAP_FLUSH_EXPLICIT_BIT)));
    } else {
        glNamedBufferData(gl_buffer.handle, allocate_size, nullptr, GL_STREAM_DRAW);
    }
}

OGLStreamBuffer::~OGLStreamBuffer() {
    if (persistent) {
        glUnmapNamedBuffer(gl_buffer.handle);
    }
    gl_buffer.Release();
}

GLuint OGLStreamBuffer::GetHandle() const {
    return gl_buffer.handle;
}

GLsizeiptr OGLStreamBuffer::GetSize() const {
    return buffer_size;
}

std::tuple<u8*, GLintptr, bool> OGLStreamBuffer::Map(GLsizeiptr size, GLintptr alignment) {
    ASSERT(size <= buffer_size);
    ASSERT(alignment <= buffer_size);
    mapped_size = size;

    if (alignment > 0) {
        buffer_pos = Common::AlignUp<std::size_t>(buffer_pos, alignment);
    }

    bool invalidate = false;
    if (buffer_pos + size > buffer_size) {
        buffer_pos = 0;
        invalidate = true;

        if (persistent) {
            glUnmapNamedBuffer(gl_buffer.handle);
        }
    }

    if (invalidate || !persistent) {
        MICROPROFILE_SCOPE(OpenGL_StreamBuffer);
        GLbitfield flags = GL_MAP_WRITE_BIT | (persistent ? GL_MAP_PERSISTENT_BIT : 0) |
                           (coherent ? GL_MAP_COHERENT_BIT : GL_MAP_FLUSH_EXPLICIT_BIT) |
                           (invalidate ? GL_MAP_INVALIDATE_BUFFER_BIT : GL_MAP_UNSYNCHRONIZED_BIT);
        mapped_ptr = static_cast<u8*>(
            glMapNamedBufferRange(gl_buffer.handle, buffer_pos, buffer_size - buffer_pos, flags));
        mapped_offset = buffer_pos;
    }

    return std::make_tuple(mapped_ptr + buffer_pos - mapped_offset, buffer_pos, invalidate);
}

void OGLStreamBuffer::Unmap(GLsizeiptr size) {
    ASSERT(size <= mapped_size);

    if (!coherent && size > 0) {
        glFlushMappedNamedBufferRange(gl_buffer.handle, buffer_pos - mapped_offset, size);
    }

    if (!persistent) {
        glUnmapNamedBuffer(gl_buffer.handle);
    }

    buffer_pos += size;
}

} // namespace OpenGL
