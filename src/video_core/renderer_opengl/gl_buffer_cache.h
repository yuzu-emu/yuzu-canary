// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>

#include "common/common_types.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/rasterizer_cache.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_stream_buffer.h"

namespace Core {
class System;
}

namespace OpenGL {

class OGLStreamBuffer;
class RasterizerOpenGL;

class CachedBufferBlock;

using Buffer = std::shared_ptr<CachedBufferBlock>;

class CachedBufferBlock : public VideoCommon::BufferBlock {
public:
    explicit CachedBufferBlock(CacheAddr cache_addr, const std::size_t size);
    ~CachedBufferBlock();

    const GLuint* GetHandle() const {
        return &gl_buffer.handle;
    }

private:
    OGLBuffer gl_buffer{};
};

class OGLBufferCache final : public VideoCommon::BufferCache<Buffer, GLuint, OGLStreamBuffer> {
public:
    explicit OGLBufferCache(RasterizerOpenGL& rasterizer, Core::System& system,
                            std::size_t stream_size);
    ~OGLBufferCache();

    const GLuint* GetEmptyBuffer(std::size_t) override;

protected:
    Buffer CreateBlock(CacheAddr cache_addr, std::size_t size) override;

    void WriteBarrier() override;

    const GLuint* ToHandle(const Buffer& buffer) override;

    void UploadBlockData(const Buffer& buffer, std::size_t offset, std::size_t size,
                         const u8* data) override;

    void DownloadBlockData(const Buffer& buffer, std::size_t offset, std::size_t size,
                           u8* data) override;

    void CopyBlock(const Buffer& src, const Buffer& dst, std::size_t src_offset,
                   std::size_t dst_offset, std::size_t size) override;
};

} // namespace OpenGL
