// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>

#include "common/common_types.h"
#include "video_core/buffer_cache.h"
#include "video_core/rasterizer_cache.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_stream_buffer.h"

namespace Core {
class System;
}

namespace OpenGL {

class OGLStreamBuffer;
class RasterizerOpenGL;

class OGLBufferCache final : public VideoCommon::BufferCache<OGLBuffer, GLuint, OGLStreamBuffer> {
public:
    explicit OGLBufferCache(RasterizerOpenGL& rasterizer, Core::System& system,
                            std::size_t stream_size);
    ~OGLBufferCache();

    const GLuint* GetEmptyBuffer(std::size_t) override;

protected:
    OGLBuffer CreateBuffer(std::size_t size) override;

    const GLuint* ToHandle(const OGLBuffer& buffer) override;

    void UploadBufferData(const OGLBuffer& buffer, std::size_t offset, std::size_t size,
                          const u8* data) override;

    void DownloadBufferData(const OGLBuffer& buffer, std::size_t offset, std::size_t size,
                            u8* data) override;

    void CopyBufferData(const OGLBuffer& src, const OGLBuffer& dst, std::size_t src_offset,
                        std::size_t dst_offset, std::size_t size) override;
};

} // namespace OpenGL
