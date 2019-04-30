// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <glad/glad.h>

namespace OpenGL {

class Device {
public:
    Device();

    std::size_t GetUniformBufferAlignment() const {
        return uniform_buffer_alignment;
    }

    bool HasVariableAoffi() const {
        return has_variable_aoffi;
    }

    bool IsTuringGPU() const {
        return GLAD_GL_NV_mesh_shader;
    }

private:
    static bool TestVariableAoffi();

    std::size_t uniform_buffer_alignment{};
    bool has_variable_aoffi{};
};

} // namespace OpenGL
