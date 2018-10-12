// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"
#include "common/swap.h"

namespace Service::HID {
class ControllerBase {
public:
    ControllerBase() = default;
    virtual ~ControllerBase() = 0;

    // Called when the controller is initialized
    virtual void OnInit() = 0;

    // When the controller is released
    virtual void OnRelease() = 0;

    // When the controller is requesting an update for the shared memory
    virtual void OnUpdate(u8* data, std::size_t size) = 0;

    // Called when input devices should be loaded
    virtual void OnLoadInputDevices() = 0;

    void ActivateController();

    void DeactivateController();

    bool IsControllerActivated() const;

protected:
    bool is_activated{false};

    struct CommonHeader {
        s64_le timestamp;
        s64_le total_entry_count;
        s64_le last_entry_index;
        s64_le entry_count;
    };
    static_assert(sizeof(CommonHeader) == 0x20, "CommonHeader is an invalid size");
};
} // namespace Service::HID
