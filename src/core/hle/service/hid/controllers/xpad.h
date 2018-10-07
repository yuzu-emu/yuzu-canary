// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/swap.h"
#include "core/frontend/input.h"
#include "core/hle/service/hid/controllers/controller_base.h"

namespace Service::HID {
class Controller_XPad final : public ControllerBase {
public:
    Controller_XPad();

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
    static_assert(sizeof(AnalogStick) == 0x8, "AnalogStick is an invalid size");

    struct XPadState {
        s64_le sampling_number;
        s64_le sampling_number2;
        s32_le attributes;
        u32_le pad_states;
        AnalogStick x_stick;
        AnalogStick y_stick;
    };
    static_assert(sizeof(XPadState) == 0x28, "XPadState is an invalid size");

    struct XPadEntry {
        CommonHeader header;
        std::array<XPadState, 17> pad_states{};
        INSERT_PADDING_BYTES(0x138);
    };
    static_assert(sizeof(XPadEntry) == 0x400, "XPadEntry is an invalid size");

    struct SharedMemory {
        std::array<XPadEntry, 4> shared_memory_entries{};
    };
    static_assert(sizeof(SharedMemory) == 0x1000, "SharedMemory is an invalid size");
    SharedMemory shared_memory{};
};
} // namespace Service::HID
