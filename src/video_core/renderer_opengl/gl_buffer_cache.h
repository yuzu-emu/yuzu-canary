// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <unordered_map>

#include "common/common_types.h"
#include "video_core/rasterizer_cache.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_stream_buffer.h"

namespace OpenGL {

struct CachedBufferEntry final {
    VAddr GetAddr() const {
        return addr;
    }

    size_t GetSizeInBytes() const {
        return size;
    }

    VAddr addr;
    size_t size;
    GLintptr offset;
    size_t alignment;
};

class OGLBufferCache final : public RasterizerCache<std::shared_ptr<CachedBufferEntry>> {
public:
    OGLBufferCache(size_t size);

    GLintptr UploadMemory(Tegra::GPUVAddr gpu_addr, size_t size, size_t alignment = 4,
                          bool cache = true);

    GLintptr UploadHostMemory(const void* raw_pointer, size_t size, size_t alignment = 4);

    void Map(size_t max_size);
    void Unmap();

    GLuint GetHandle();

protected:
    void AlignBuffer(size_t alignment);

private:
    OGLStreamBuffer stream_buffer;

    u8* buffer_ptr;
    GLintptr buffer_offset;
    GLintptr buffer_offset_base;
};

} // namespace OpenGL
