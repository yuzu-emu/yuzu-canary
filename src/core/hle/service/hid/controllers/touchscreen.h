// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/bit_field.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/swap.h"
#include "core/frontend/input.h"
#include "core/hle/service/hid/controllers/controller_base.h"

namespace Service::HID {
class Controller_Touchscreen final : public ControllerBase {
public:
    Controller_Touchscreen();
    ~Controller_Touchscreen() override;

    // Called when the controller is initialized
    void OnInit() override;

    // When the controller is released
    void OnRelease() override;

    // When the controller is requesting an update for the shared memory
    void OnUpdate(u8* data, std::size_t size) override;

    // Called when input devices should be loaded
    void OnLoadInputDevices() override;

private:
    struct Attributes {
        union {
            u32 raw{};
            BitField<0, 1, u32_le> start_touch;
            BitField<1, 1, u32_le> end_touch;
        };
    };
    static_assert(sizeof(Attributes) == 0x4, "Attributes is an invalid size");

    struct TouchState {
        u64_le delta_time;
        Attributes attribute;
        u32_le finger;
        u32_le x;
        u32_le y;
        u32_le diameter_x;
        u32_le diameter_y;
        u32_le rotation_angle;
    };
    static_assert(sizeof(TouchState) == 0x28, "Touchstate is an invalid size");

    struct TouchScreenEntry {
        s64_le sampling_number;
        s64_le sampling_number2;
        s32_le entry_count;
        std::array<TouchState, 16> states;
    };
    static_assert(sizeof(TouchScreenEntry) == 0x298, "TouchScreenEntry is an invalid size");

    struct TouchScreenSharedMemory {
        CommonHeader header;
        std::array<TouchScreenEntry, 17> shared_memory_entries{};
        INSERT_PADDING_BYTES(0x3c8);
    };
    static_assert(sizeof(TouchScreenSharedMemory) == 0x3000,
                  "TouchScreenSharedMemory is an invalid size");
    TouchScreenSharedMemory shared_memory{};
    std::unique_ptr<Input::TouchDevice> touch_device;
    s64_le last_touch{};
};
} // namespace Service::HID
