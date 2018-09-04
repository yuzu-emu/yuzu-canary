// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/alignment.h"
#include "common/assert.h"
#include "core/core.h"
#include "core/memory.h"
#include "video_core/renderer_opengl/gl_buffer_cache.h"

namespace OpenGL {

OGLBufferCache::OGLBufferCache(size_t size) : stream_buffer(GL_ARRAY_BUFFER, size) {}

GLintptr OGLBufferCache::UploadMemory(Tegra::GPUVAddr gpu_addr, size_t size, size_t alignment,
                                      bool cache) {
    auto& memory_manager = Core::System::GetInstance().GPU().MemoryManager();
    const boost::optional<VAddr> cpu_addr{memory_manager.GpuToCpuAddress(gpu_addr)};

    // Cache management is a big overhead, so only cache entries with a given size.
    // TODO: Figure out which size is the best for given games.
    cache &= size > 4096;

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
    GLintptr uploaded_offset = buffer_offset;

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

GLintptr OGLBufferCache::UploadHostMemory(const void* raw_pointer, size_t size, size_t alignment) {
    AlignBuffer(alignment);
    std::memcpy(buffer_ptr, raw_pointer, size);
    GLintptr uploaded_offset = buffer_offset;

    buffer_ptr += size;
    buffer_offset += size;
    return uploaded_offset;
}

void OGLBufferCache::Map(size_t max_size) {
    bool invalidate;
    std::tie(buffer_ptr, buffer_offset_base, invalidate) =
        stream_buffer.Map(static_cast<GLsizeiptr>(max_size), 4);
    buffer_offset = buffer_offset_base;

    if (invalidate) {
        InvalidateAll();
    }
}
void OGLBufferCache::Unmap() {
    stream_buffer.Unmap(buffer_offset - buffer_offset_base);
}

GLuint OGLBufferCache::GetHandle() {
    return stream_buffer.GetHandle();
}

void OGLBufferCache::AlignBuffer(size_t alignment) {
    // Align the offset, not the mapped pointer
    GLintptr offset_aligned =
        static_cast<GLintptr>(Common::AlignUp(static_cast<size_t>(buffer_offset), alignment));
    buffer_ptr += offset_aligned - buffer_offset;
    buffer_offset = offset_aligned;
}

} // namespace OpenGL
