// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include <memory>

#include "common/alignment.h"
#include "core/core.h"
#include "core/memory.h"
#include "video_core/renderer_opengl/gl_buffer_cache.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"

namespace OpenGL {

OGLBufferCache::OGLBufferCache(RasterizerOpenGL& rasterizer, std::size_t size)
    : RasterizerCache{rasterizer}, stream_buffer(GL_ARRAY_BUFFER, size) {}

GLintptr OGLBufferCache::UploadMemory(Tegra::GPUVAddr gpu_addr, std::size_t size,
                                      std::size_t alignment, bool cache) {
    auto& memory_manager = Core::System::GetInstance().GPU().MemoryManager();
    const std::optional<VAddr> cpu_addr{memory_manager.GpuToCpuAddress(gpu_addr)};

    // Cache management is a big overhead, so only cache entries with a given size.
    // TODO: Figure out which size is the best for given games.
    cache &= size >= 2048;

    if (cache) {
        auto entry = TryGet(*cpu_addr);
        if (entry) {
            if (entry->size >= size && entry->alignment == alignment) {
                return entry->offset;
            }
            Unregister(entry);
        }
    }

    AlignBuffer(alignment);
    const GLintptr uploaded_offset = buffer_offset;

    Memory::ReadBlock(*cpu_addr, buffer_ptr, size);

    buffer_ptr += size;
    buffer_offset += size;

    if (cache) {
        auto entry = std::make_shared<CachedBufferEntry>();
        entry->offset = uploaded_offset;
        entry->size = size;
        entry->alignment = alignment;
        entry->addr = *cpu_addr;
        Register(entry);
    }

    return uploaded_offset;
}

GLintptr OGLBufferCache::UploadHostMemory(const void* raw_pointer, std::size_t size,
                                          std::size_t alignment) {
    AlignBuffer(alignment);
    std::memcpy(buffer_ptr, raw_pointer, size);
    const GLintptr uploaded_offset = buffer_offset;

    buffer_ptr += size;
    buffer_offset += size;
    return uploaded_offset;
}

std::tuple<u8*, GLintptr> OGLBufferCache::ReserveMemory(std::size_t size, std::size_t alignment) {
    AlignBuffer(alignment);
    u8* const uploaded_ptr = buffer_ptr;
    const GLintptr uploaded_offset = buffer_offset;

    buffer_ptr += size;
    buffer_offset += size;
    return std::make_tuple(uploaded_ptr, uploaded_offset);
}

bool OGLBufferCache::Map(std::size_t max_size) {
    bool invalidate;
    std::tie(buffer_ptr, buffer_offset_base, invalidate) =
        stream_buffer.Map(static_cast<GLsizeiptr>(max_size), 4);
    buffer_offset = buffer_offset_base;

    if (invalidate) {
        InvalidateAll();
    }
    return invalidate;
}

void OGLBufferCache::Unmap() {
    stream_buffer.Unmap(buffer_offset - buffer_offset_base);
}

GLuint OGLBufferCache::GetHandle() const {
    return stream_buffer.GetHandle();
}

void OGLBufferCache::AlignBuffer(std::size_t alignment) {
    // Align the offset, not the mapped pointer
    const GLintptr offset_aligned =
        static_cast<GLintptr>(Common::AlignUp(static_cast<std::size_t>(buffer_offset), alignment));
    buffer_ptr += offset_aligned - buffer_offset;
    buffer_offset = offset_aligned;
}

} // namespace OpenGL
