// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include "common/assert.h"
#include "common/common_types.h"
#include "core/core.h"
#include "core/memory.h"
#include "video_core/renderer_opengl/gl_buffer_cache.h"
#include "video_core/renderer_opengl/gl_primitive_assembler.h"

namespace OpenGL {

constexpr u32 TRIANGLES_PER_QUAD = 6;
constexpr std::array<u32, TRIANGLES_PER_QUAD> QUAD_MAP = {0, 1, 2, 0, 2, 3};

PrimitiveAssembler::PrimitiveAssembler(OGLBufferCache& buffer_cache) : buffer_cache(buffer_cache) {}

PrimitiveAssembler::~PrimitiveAssembler() = default;

std::size_t PrimitiveAssembler::CalculateQuadSize(u32 count) const {
    ASSERT_MSG(count % 4 == 0, "Quad count is expected to be a multiple of 4");
    return (count / 4) * TRIANGLES_PER_QUAD * sizeof(GLuint);
}

GLintptr PrimitiveAssembler::MakeQuadArray(u32 first, u32 count) {
    const std::size_t size{CalculateQuadSize(count)};
    auto [dst_pointer, index_offset] = buffer_cache.ReserveMemory(size);

    for (u32 primitive = 0; primitive < count / 4; ++primitive) {
        for (u32 i = 0; i < TRIANGLES_PER_QUAD; ++i) {
            const u32 index = first + primitive * 4 + QUAD_MAP[i];
            std::memcpy(dst_pointer, &index, sizeof(index));
            dst_pointer += sizeof(index);
        }
    }

    return index_offset;
}

GLintptr PrimitiveAssembler::MakeQuadIndexed(Tegra::GPUVAddr gpu_addr, std::size_t index_size,
                                             u32 count) {
    const std::size_t map_size{CalculateQuadSize(count)};
    auto [dst_pointer, index_offset] = buffer_cache.ReserveMemory(map_size);

    auto& memory_manager = Core::System::GetInstance().GPU().MemoryManager();
    const u8* source{memory_manager.GetPointer(gpu_addr)};

    for (u32 primitive = 0; primitive < count / 4; ++primitive) {
        for (std::size_t i = 0; i < TRIANGLES_PER_QUAD; ++i) {
            const u32 index = primitive * 4 + QUAD_MAP[i];
            const u8* src_offset = source + (index * index_size);

            std::memcpy(dst_pointer, src_offset, index_size);
            dst_pointer += index_size;
        }
    }

    return index_offset;
}

} // namespace OpenGL
