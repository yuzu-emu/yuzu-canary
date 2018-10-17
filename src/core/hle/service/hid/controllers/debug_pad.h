// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/swap.h"
#include "core/hle/service/hid/controllers/controller_base.h"

namespace Service::HID {
class Controller_DebugPad final : public ControllerBase {
public:
    Controller_DebugPad();

    // Called when the controller is initialized
    void OnInit() override;

    // When the controller is released
    void OnRelease() override;

    // When the controller is requesting an update for the shared memory
    void OnUpdate(u8* data, std::size_t size) override;

    // Called when input devices should be loaded
    void OnLoadInputDevices() override;

private:
    struct AnalogStick {
        s32_le x;
        s32_le y;
    };
    static_assert(sizeof(AnalogStick) == 0x8);

    struct PadStates {
        s64_le sampling_number;
        s64_le sampling_number2;
        u32_le attribute;
        u32_le button_state;
        AnalogStick r_stick;
        AnalogStick l_stick;
    };
    static_assert(sizeof(PadStates) == 0x28, "PadStates is an invalid state");

    struct SharedMemory {
        CommonHeader header;
        std::array<PadStates, 17> pad_states;
        INSERT_PADDING_BYTES(0x138);
    };
    static_assert(sizeof(SharedMemory) == 0x400, "SharedMemory is an invalid size");
    SharedMemory shared_memory{};
};
} // namespace Service::HID
