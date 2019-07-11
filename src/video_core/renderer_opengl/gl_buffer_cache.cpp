// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>

#include <glad/glad.h>

#include "common/assert.h"
#include "video_core/renderer_opengl/gl_buffer_cache.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"

namespace OpenGL {

OGLBufferCache::OGLBufferCache(RasterizerOpenGL& rasterizer, Core::System& system,
                               std::size_t stream_size)
    : VideoCommon::BufferCache<OGLBuffer, GLuint, OGLStreamBuffer>{
          rasterizer, system, std::make_unique<OGLStreamBuffer>(stream_size, true)} {}

OGLBufferCache::~OGLBufferCache() = default;

OGLBuffer OGLBufferCache::CreateBuffer(std::size_t size) {
    OGLBuffer buffer;
    buffer.Create();
    glNamedBufferData(buffer.handle, static_cast<GLsizeiptr>(size), nullptr, GL_DYNAMIC_DRAW);
    return buffer;
}

const GLuint* OGLBufferCache::ToHandle(const OGLBuffer& buffer) {
    return &buffer.handle;
}

const GLuint* OGLBufferCache::GetEmptyBuffer(std::size_t) {
    static const GLuint null_buffer = 0;
    return &null_buffer;
}

void OGLBufferCache::UploadBufferData(const OGLBuffer& buffer, std::size_t offset, std::size_t size,
                                      const u8* data) {
    glNamedBufferSubData(buffer.handle, static_cast<GLintptr>(offset),
                         static_cast<GLsizeiptr>(size), data);
}

void OGLBufferCache::DownloadBufferData(const OGLBuffer& buffer, std::size_t offset,
                                        std::size_t size, u8* data) {
    glGetNamedBufferSubData(buffer.handle, static_cast<GLintptr>(offset),
                            static_cast<GLsizeiptr>(size), data);
}

void OGLBufferCache::CopyBufferData(const OGLBuffer& src, const OGLBuffer& dst,
                                    std::size_t src_offset, std::size_t dst_offset,
                                    std::size_t size) {
    glCopyNamedBufferSubData(src.handle, dst.handle, static_cast<GLintptr>(src_offset),
                             static_cast<GLintptr>(dst_offset), static_cast<GLsizeiptr>(size));
}

} // namespace OpenGL
