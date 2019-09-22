// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include "common/common_types.h"

namespace OpenGL {

class Device {
public:
    explicit Device();
    explicit Device(std::nullptr_t);

    std::size_t GetUniformBufferAlignment() const {
        return uniform_buffer_alignment;
    }

    std::size_t GetShaderStorageBufferAlignment() const {
        return shader_storage_alignment;
    }

    u32 GetMaxVertexAttributes() const {
        return max_vertex_attributes;
    }

    u32 GetMaxVaryings() const {
        return max_varyings;
    }

    bool HasWarpIntrinsics() const {
        return has_warp_intrinsics;
    }

    bool HasVertexViewportLayer() const {
        return has_vertex_viewport_layer;
    }

    bool HasImageLoadFormatted() const {
        return has_image_load_formatted;
    }

    bool HasVariableAoffi() const {
        return has_variable_aoffi;
    }

    bool HasComponentIndexingBug() const {
        return has_component_indexing_bug;
    }

    bool HasPreciseBug() const {
        return has_precise_bug;
    }

private:
    static bool TestVariableAoffi();
    static bool TestComponentIndexingBug();
    static bool TestPreciseBug();

    std::size_t uniform_buffer_alignment{};
    std::size_t shader_storage_alignment{};
    u32 max_vertex_attributes{};
    u32 max_varyings{};
    bool has_warp_intrinsics{};
    bool has_vertex_viewport_layer{};
    bool has_image_load_formatted{};
    bool has_variable_aoffi{};
    bool has_component_indexing_bug{};
    bool has_precise_bug{};
};

} // namespace OpenGL
