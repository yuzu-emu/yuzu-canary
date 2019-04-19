// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>

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

private:
    static bool TestVariableAoffi();

    std::size_t uniform_buffer_alignment{};
    bool has_variable_aoffi{};
};

} // namespace OpenGL
