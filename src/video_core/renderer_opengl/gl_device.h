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

    bool HasVariableAoffi() const {
        return has_variable_aoffi;
    }

    bool HasComponentIndexingBug() const {
        return has_component_indexing_bug;
    }

private:
    static bool TestVariableAoffi();
    static bool TestComponentIndexingBug();

    std::size_t uniform_buffer_alignment{};
    std::size_t shader_storage_alignment{};
    u32 max_vertex_attributes{};
    u32 max_varyings{};
    bool has_variable_aoffi{};
    bool has_component_indexing_bug{};
};

} // namespace OpenGL
