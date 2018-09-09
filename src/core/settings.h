// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <string>
#include "common/common_types.h"

namespace Settings {

namespace NativeButton {
enum Values {
    A,
    B,
    X,
    Y,
    LStick,
    RStick,
    L,
    R,
    ZL,
    ZR,
    Plus,
    Minus,

    DLeft,
    DUp,
    DRight,
    DDown,

    LStick_Left,
    LStick_Up,
    LStick_Right,
    LStick_Down,

    RStick_Left,
    RStick_Up,
    RStick_Right,
    RStick_Down,

    SL,
    SR,

    Home,
    Screenshot,

    NumButtons,
};

constexpr int BUTTON_HID_BEGIN = A;
constexpr int BUTTON_NS_BEGIN = Home;

constexpr int BUTTON_HID_END = BUTTON_NS_BEGIN;
constexpr int BUTTON_NS_END = NumButtons;

constexpr int NUM_BUTTONS_HID = BUTTON_HID_END - BUTTON_HID_BEGIN;
constexpr int NUM_BUTTONS_NS = BUTTON_NS_END - BUTTON_NS_BEGIN;

static const std::array<const char*, NumButtons> mapping = {{
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

} // namespace NativeButton

namespace NativeAnalog {
enum Values {
    LStick,
    RStick,

    NumAnalogs,
};

constexpr int STICK_HID_BEGIN = LStick;
constexpr int STICK_HID_END = NumAnalogs;
constexpr int NUM_STICKS_HID = NumAnalogs;

static const std::array<const char*, NumAnalogs> mapping = {{
    "lstick",
    "rstick",
}};
} // namespace NativeAnalog

struct Values {
    // System
    bool use_docked_mode;
    std::string username;
    int language_index;

    // Controls
    std::array<std::string, NativeButton::NumButtons> buttons;
    std::array<std::string, NativeAnalog::NumAnalogs> analogs;
    std::string motion_device;
    std::string touch_device;

    // Core
    bool use_cpu_jit;
    bool use_multi_core;

    // Data Storage
    bool use_virtual_sd;
    std::string nand_dir;
    std::string sdmc_dir;

    // Renderer
    float resolution_factor;
    bool use_frame_limit;
    u16 frame_limit;
    bool use_accurate_framebuffers;

    float bg_red;
    float bg_green;
    float bg_blue;

    std::string log_filter;

    bool use_dev_keys;

    // Audio
    std::string sink_id;
    bool enable_audio_stretching;
    std::string audio_device_id;
    float volume;

    // Debugging
    bool use_gdbstub;
    u16 gdbstub_port;
} extern values;

void Apply();
} // namespace Settings
