// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>

#include <glad/glad.h>

#include "common/assert.h"
#include "common/microprofile.h"
#include "video_core/renderer_opengl/gl_buffer_cache.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"

namespace OpenGL {

MICROPROFILE_DEFINE(OpenGL_Buffer_Download, "OpenGL", "Buffer Download", MP_RGB(192, 192, 128));

CachedBufferBlock::CachedBufferBlock(CacheAddr cache_addr, const std::size_t size)
    : VideoCommon::BufferBlock{cache_addr, size} {
    gl_buffer.Create();
    glNamedBufferData(gl_buffer.handle, static_cast<GLsizeiptr>(size), nullptr, GL_DYNAMIC_DRAW);
}

CachedBufferBlock::~CachedBufferBlock() = default;

OGLBufferCache::OGLBufferCache(RasterizerOpenGL& rasterizer, Core::System& system,
                               std::size_t stream_size)
    : VideoCommon::BufferCache<Buffer, GLuint, OGLStreamBuffer>{
          rasterizer, system, std::make_unique<OGLStreamBuffer>(stream_size, true)} {}

OGLBufferCache::~OGLBufferCache() = default;

Buffer OGLBufferCache::CreateBlock(CacheAddr cache_addr, std::size_t size) {
    return std::make_shared<CachedBufferBlock>(cache_addr, size);
}

void OGLBufferCache::WriteBarrier() {
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
}

const GLuint* OGLBufferCache::ToHandle(const Buffer& buffer) {
    return buffer->GetHandle();
}

const GLuint* OGLBufferCache::GetEmptyBuffer(std::size_t) {
    static const GLuint null_buffer = 0;
    return &null_buffer;
}

void OGLBufferCache::UploadBlockData(const Buffer& buffer, std::size_t offset, std::size_t size,
                                     const u8* data) {
    glNamedBufferSubData(*buffer->GetHandle(), static_cast<GLintptr>(offset),
                         static_cast<GLsizeiptr>(size), data);
}

void OGLBufferCache::DownloadBlockData(const Buffer& buffer, std::size_t offset, std::size_t size,
                                       u8* data) {
    MICROPROFILE_SCOPE(OpenGL_Buffer_Download);
    glGetNamedBufferSubData(*buffer->GetHandle(), static_cast<GLintptr>(offset),
                            static_cast<GLsizeiptr>(size), data);
}

void OGLBufferCache::CopyBlock(const Buffer& src, const Buffer& dst, std::size_t src_offset,
                               std::size_t dst_offset, std::size_t size) {
    glCopyNamedBufferSubData(*src->GetHandle(), *dst->GetHandle(),
                             static_cast<GLintptr>(src_offset), static_cast<GLintptr>(dst_offset),
                             static_cast<GLsizeiptr>(size));
}

} // namespace OpenGL
