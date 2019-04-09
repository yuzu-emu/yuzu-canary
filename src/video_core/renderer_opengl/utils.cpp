// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <string>
#include <fmt/format.h>
#include <glad/glad.h>
#include "common/assert.h"
#include "common/common_types.h"
#include "video_core/renderer_opengl/utils.h"

namespace OpenGL {

BindBuffersRangePushBuffer::BindBuffersRangePushBuffer(GLenum target) : target{target} {}

BindBuffersRangePushBuffer::~BindBuffersRangePushBuffer() = default;

void BindBuffersRangePushBuffer::Setup(GLuint first_) {
    first = first_;
    buffers.clear();
    offsets.clear();
    sizes.clear();
}

void BindBuffersRangePushBuffer::Push(GLuint buffer, GLintptr offset, GLsizeiptr size) {
    buffers.push_back(buffer);
    offsets.push_back(offset);
    sizes.push_back(size);
}

void BindBuffersRangePushBuffer::Bind() const {
    const std::size_t count{buffers.size()};
    DEBUG_ASSERT(count == offsets.size() && count == sizes.size());
    if (count == 0) {
        return;
    }
    glBindBuffersRange(target, first, static_cast<GLsizei>(count), buffers.data(), offsets.data(),
                       sizes.data());
}

void LabelGLObject(GLenum identifier, GLuint handle, VAddr addr, std::string extra_info) {
    if (!GLAD_GL_KHR_debug) {
        return; // We don't need to throw an error as this is just for debugging
    }
    const std::string nice_addr = fmt::format("0x{:016x}", addr);
    std::string object_label;

    if (extra_info.empty()) {
        switch (identifier) {
        case GL_TEXTURE:
            object_label = "Texture@" + nice_addr;
            break;
        case GL_PROGRAM:
            object_label = "Shader@" + nice_addr;
            break;
        default:
            object_label = fmt::format("Object(0x{:x})@{}", identifier, nice_addr);
            break;
        }
    } else {
        object_label = extra_info + '@' + nice_addr;
    }
    glObjectLabel(identifier, handle, -1, static_cast<const GLchar*>(object_label.c_str()));
}

} // namespace OpenGL