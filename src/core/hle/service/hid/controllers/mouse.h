// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include "common/common_types.h"
#include "common/swap.h"
#include "core/hle/service/hid/controllers/controller_base.h"

namespace Service::HID {
class Controller_Mouse final : public ControllerBase {
public:
    Controller_Mouse();

    // Called when the controller is initialized
    void OnInit() override;

    // When the controller is released
    void OnRelease() override;

    // When the controller is requesting an update for the shared memory
    void OnUpdate(u8* data, std::size_t size) override;

    // Called when input devices should be loaded
    void OnLoadInputDevices() override;

private:
    struct MouseState {
        s64_le sampling_number;
        s64_le sampling_number2;
        s32_le x;
        s32_le y;
        s32_le delta_x;
        s32_le delta_y;
        s32_le mouse_wheel;
        s32_le button;
        s32_le attribute;
    };
    static_assert(sizeof(MouseState) == 0x30, "MouseState is an invalid size");

    struct SharedMemory {
        CommonHeader header;
        std::array<MouseState, 17> mouse_states;
    };
    SharedMemory shared_memory{};
};
} // namespace Service::HID
