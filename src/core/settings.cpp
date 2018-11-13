// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/core.h"
#include "core/gdbstub/gdbstub.h"
#include "core/hle/service/hid/hid.h"
#include "core/settings.h"
#include "video_core/renderer_base.h"

namespace Settings {

namespace NativeButton {
const std::array<const char*, NumButtons> mapping = {{
    "button_a",
    "button_b",
    "button_x",
    "button_y",
    "button_lstick",
    "button_rstick",
    "button_l",
    "button_r",
    "button_zl",
    "button_zr",
    "button_plus",
    "button_minus",
    "button_dleft",
    "button_dup",
    "button_dright",
    "button_ddown",
    "button_lstick_left",
    "button_lstick_up",
    "button_lstick_right",
    "button_lstick_down",
    "button_rstick_left",
    "button_rstick_up",
    "button_rstick_right",
    "button_rstick_down",
    "button_sl",
    "button_sr",
    "button_home",
    "button_screenshot",
}};
}

namespace NativeAnalog {
const std::array<const char*, NumAnalogs> mapping = {{
    "lstick",
    "rstick",
}};
}

namespace NativeMouseButton {
const std::array<const char*, NumMouseButtons> mapping = {{
    "left",
    "right",
    "middle",
    "forward",
    "back",
}};
}

Values values = {};

void Apply() {
    GDBStub::SetServerPort(values.gdbstub_port);
    GDBStub::ToggleServer(values.use_gdbstub);

    auto& system_instance = Core::System::GetInstance();
    if (system_instance.IsPoweredOn()) {
        system_instance.Renderer().RefreshBaseSettings();
    }

    Service::HID::ReloadInputDevices();
}

} // namespace Settings
