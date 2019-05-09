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

    u32 GetMaxVertexAttributes() const {
        return max_vertex_attributes;
    }

    u32 GetMaxVaryings() const {
        return max_varyings;
    }

    bool HasVariableAoffi() const {
        return has_variable_aoffi;
    }

private:
    static bool TestVariableAoffi();

    std::size_t uniform_buffer_alignment{};
    u32 max_vertex_attributes{};
    u32 max_varyings{};
    bool has_variable_aoffi{};
};

} // namespace OpenGL
