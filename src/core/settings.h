// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include "common/common_types.h"
#include "input_common/motion_emu.h"

namespace Settings {

constexpr u64 DEFAULT_PER_GAME = 0;

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

struct PerGameValues {
    void LogSettings();

    // Controls
    std::array<std::string, NativeButton::NumButtons> buttons;
    std::array<std::string, NativeAnalog::NumAnalogs> analogs;
    std::string motion_device;
    std::string touch_device;

    // System
    bool use_docked_mode;

    // Renderer
    float resolution_factor;
    bool use_frame_limit;
    u16 frame_limit;

    float bg_red;
    float bg_green;
    float bg_blue;

    // Audio
    std::string sink_id;
    bool enable_audio_stretching;
    std::string audio_device_id;
    float volume;

    // Debugging
    std::string program_args;

    // Add-Ons
    std::vector<std::string> disabled_patches;
};

struct Values {
    Values();

    // Per-Game
    PerGameValues default_game;

    void SetUpdateCurrentGameFunction(std::function<bool(u64, PerGameValues&)> new_function);
    u64 CurrentTitleID() const;
    void SetCurrentTitleID(u64 title_id);

    PerGameValues& operator[](u64 title_id);
    const PerGameValues& operator[](u64 title_id) const;
    PerGameValues* operator->();
    const PerGameValues* operator->() const;
    PerGameValues& operator*();
    const PerGameValues& operator*() const;

    // Core
    bool use_cpu_jit;
    bool use_multi_core;

    // System
    std::string username;
    bool enable_nfc;
    int current_user;
    int language_index;

    // Renderer
    bool use_accurate_gpu_emulation;

    // Input
    std::atomic_bool is_device_reload_pending{true};

    // Data Storage
    bool use_virtual_sd;
    std::string nand_dir;
    std::string sdmc_dir;

    // Debugging
    std::string log_filter;
    bool use_dev_keys;
    bool use_gdbstub;
    u16 gdbstub_port;

    // WebService
    bool enable_telemetry;
    std::string web_api_url;
    std::string yuzu_username;
    std::string yuzu_token;

private:
    u64 current_title_id = 0;
    PerGameValues current_game;

    std::function<bool(u64, PerGameValues&)> update_current_game = [](u64 in, PerGameValues& val) {
        return false;
    };
} extern values;

void Apply();
} // namespace Settings
