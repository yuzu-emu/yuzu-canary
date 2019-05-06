// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>
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

extern const std::array<const char*, NumButtons> mapping;

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

extern const std::array<const char*, NumAnalogs> mapping;
} // namespace NativeAnalog

namespace NativeMouseButton {
enum Values {
    Left,
    Right,
    Middle,
    Forward,
    Back,

    NumMouseButtons,
};

constexpr int MOUSE_HID_BEGIN = Left;
constexpr int MOUSE_HID_END = NumMouseButtons;
constexpr int NUM_MOUSE_HID = NumMouseButtons;

extern const std::array<const char*, NumMouseButtons> mapping;
} // namespace NativeMouseButton

namespace NativeKeyboard {
enum Keys {
    None,
    Error,

    A = 4,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    N1,
    N2,
    N3,
    N4,
    N5,
    N6,
    N7,
    N8,
    N9,
    N0,
    Enter,
    Escape,
    Backspace,
    Tab,
    Space,
    Minus,
    Equal,
    LeftBrace,
    RightBrace,
    Backslash,
    Tilde,
    Semicolon,
    Apostrophe,
    Grave,
    Comma,
    Dot,
    Slash,
    CapsLockKey,

    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,

    SystemRequest,
    ScrollLockKey,
    Pause,
    Insert,
    Home,
    PageUp,
    Delete,
    End,
    PageDown,
    Right,
    Left,
    Down,
    Up,

    NumLockKey,
    KPSlash,
    KPAsterisk,
    KPMinus,
    KPPlus,
    KPEnter,
    KP1,
    KP2,
    KP3,
    KP4,
    KP5,
    KP6,
    KP7,
    KP8,
    KP9,
    KP0,
    KPDot,

    Key102,
    Compose,
    Power,
    KPEqual,

    F13,
    F14,
    F15,
    F16,
    F17,
    F18,
    F19,
    F20,
    F21,
    F22,
    F23,
    F24,

    Open,
    Help,
    Properties,
    Front,
    Stop,
    Repeat,
    Undo,
    Cut,
    Copy,
    Paste,
    Find,
    Mute,
    VolumeUp,
    VolumeDown,
    CapsLockActive,
    NumLockActive,
    ScrollLockActive,
    KPComma,

    KPLeftParenthesis,
    KPRightParenthesis,

    LeftControlKey = 0xE0,
    LeftShiftKey,
    LeftAltKey,
    LeftMetaKey,
    RightControlKey,
    RightShiftKey,
    RightAltKey,
    RightMetaKey,

    MediaPlayPause,
    MediaStopCD,
    MediaPrevious,
    MediaNext,
    MediaEject,
    MediaVolumeUp,
    MediaVolumeDown,
    MediaMute,
    MediaWebsite,
    MediaBack,
    MediaForward,
    MediaStop,
    MediaFind,
    MediaScrollUp,
    MediaScrollDown,
    MediaEdit,
    MediaSleep,
    MediaCoffee,
    MediaRefresh,
    MediaCalculator,

    NumKeyboardKeys,
};

static_assert(NumKeyboardKeys == 0xFC, "Incorrect number of keyboard keys.");

enum Modifiers {
    LeftControl,
    LeftShift,
    LeftAlt,
    LeftMeta,
    RightControl,
    RightShift,
    RightAlt,
    RightMeta,
    CapsLock,
    ScrollLock,
    NumLock,

    NumKeyboardMods,
};

constexpr int KEYBOARD_KEYS_HID_BEGIN = None;
constexpr int KEYBOARD_KEYS_HID_END = NumKeyboardKeys;
constexpr int NUM_KEYBOARD_KEYS_HID = NumKeyboardKeys;

constexpr int KEYBOARD_MODS_HID_BEGIN = LeftControl;
constexpr int KEYBOARD_MODS_HID_END = NumKeyboardMods;
constexpr int NUM_KEYBOARD_MODS_HID = NumKeyboardMods;

} // namespace NativeKeyboard

using ButtonsRaw = std::array<std::string, NativeButton::NumButtons>;
using AnalogsRaw = std::array<std::string, NativeAnalog::NumAnalogs>;
using MouseButtonsRaw = std::array<std::string, NativeMouseButton::NumMouseButtons>;
using KeyboardKeysRaw = std::array<std::string, NativeKeyboard::NumKeyboardKeys>;
using KeyboardModsRaw = std::array<std::string, NativeKeyboard::NumKeyboardMods>;

constexpr u32 JOYCON_BODY_NEON_RED = 0xFF3C28;
constexpr u32 JOYCON_BUTTONS_NEON_RED = 0x1E0A0A;
constexpr u32 JOYCON_BODY_NEON_BLUE = 0x0AB9E6;
constexpr u32 JOYCON_BUTTONS_NEON_BLUE = 0x001E1E;

enum class ControllerType {
    ProController,
    DualJoycon,
    RightJoycon,
    LeftJoycon,
};

struct PlayerInput {
    bool connected;
    ControllerType type;
    ButtonsRaw buttons;
    AnalogsRaw analogs;

    u32 body_color_right;
    u32 button_color_right;
    u32 body_color_left;
    u32 button_color_left;
};

struct TouchscreenInput {
    bool enabled;
    std::string device;

    u32 finger;
    u32 diameter_x;
    u32 diameter_y;
    u32 rotation_angle;
};

struct Values {
    // System
    bool use_docked_mode;
    std::optional<u32> rng_seed;
    // Measured in seconds since epoch
    std::optional<std::chrono::seconds> custom_rtc;
    // Set on game boot, reset on stop. Seconds difference between current time and `custom_rtc`
    std::chrono::seconds custom_rtc_differential;

    s32 current_user;
    s32 language_index;

    // Controls
    std::array<PlayerInput, 10> players;

    bool mouse_enabled;
    std::string mouse_device;
    MouseButtonsRaw mouse_buttons;

    bool keyboard_enabled;
    KeyboardKeysRaw keyboard_keys;
    KeyboardModsRaw keyboard_mods;

    bool debug_pad_enabled;
    ButtonsRaw debug_pad_buttons;
    AnalogsRaw debug_pad_analogs;

    std::string motion_device;
    TouchscreenInput touchscreen;
    std::atomic_bool is_device_reload_pending{true};

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
    bool use_compatibility_profile;
    bool use_disk_shader_cache;
    bool use_accurate_gpu_emulation;
    bool use_asynchronous_gpu_emulation;
    bool force_30fps_mode;
    u16 fps_mode;

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
    std::string program_args;
    bool dump_exefs;
    bool dump_nso;

    // WebService
    bool enable_telemetry;
    std::string web_api_url;
    std::string yuzu_username;
    std::string yuzu_token;

    // Add-Ons
    std::map<u64, std::vector<std::string>> disabled_addons;
} extern values;

void Apply();
void LogSettings();
} // namespace Settings
