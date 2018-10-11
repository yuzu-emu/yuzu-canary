// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include "common/common_types.h"
#include "common/swap.h"
#include "core/hle/service/hid/controllers/controller_base.h"

namespace Service::HID {
class Controller_Gesture final : public ControllerBase {
public:
    Controller_Gesture();

    // Called when the controller is initialized
    void OnInit() override;

    // When the controller is released
    void OnRelease() override;

    // When the controller is requesting an update for the shared memory
    void OnUpdate(u8* data, size_t size) override;

    // Called when input devices should be loaded
    void OnLoadInputDevices() override;

private:
    struct Locations {
        s32_le x;
        s32_le y;
    };

    struct GestureState {
        s64_le sampling_number;
        s64_le sampling_number2;

        s64_le detection_count;
        s32_le type;
        s32_le dir;
        s32_le x;
        s32_le y;
        s32_le delta_x;
        s32_le delta_y;
        f32 vel_x;
        f32 vel_y;
        s32_le attributes;
        f32 scale;
        f32 rotation;
        s32_le location_count;
        std::array<Locations, 4> locations;
    };
    static_assert(sizeof(GestureState) == 0x68, "GestureState is an invalid size");

    struct SharedMemory {
        CommonHeader header;
        std::array<GestureState, 17> gesture_states;
    };
    SharedMemory shared_memory{};
};
} // namespace Service::HID
