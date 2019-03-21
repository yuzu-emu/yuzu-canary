// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <vector>
#include <glad/glad.h>

#include "common/common_types.h"
#include "video_core/memory_manager.h"

namespace OpenGL {

class OGLBufferCache;

class PrimitiveAssembler {
public:
    explicit PrimitiveAssembler(OGLBufferCache& buffer_cache);
    ~PrimitiveAssembler();

    /// Calculates the size required by MakeQuadArray and MakeQuadIndexed.
    std::size_t CalculateQuadSize(u32 count) const;

    GLintptr MakeQuadArray(u32 first, u32 count);

    GLintptr MakeQuadIndexed(GPUVAddr gpu_addr, std::size_t index_size, u32 count);

private:
    OGLBufferCache& buffer_cache;
};

} // namespace OpenGL