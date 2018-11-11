// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <memory>
#include <string>
#include <QVariant>
#include "core/settings.h"

class QSettings;

class Config {
public:
    Config();
    ~Config();

    void Reload();
    void Save();

    static const std::array<int, Settings::NativeButton::NumButtons> default_buttons;
    static const std::array<std::array<int, 5>, Settings::NativeAnalog::NumAnalogs> default_analogs;
    static const std::array<int, Settings::NativeMouseButton::NumMouseButtons>
        default_mouse_buttons;
    static const std::array<int, Settings::NativeKeyboard::NumKeyboardKeys> default_keyboard_keys;
    static const std::array<int, Settings::NativeKeyboard::NumKeyboardMods> default_keyboard_mods;

private:
    void ReadValues();
    void ReadPlayerValues();
    void ReadDebugValues();
    void ReadKeyboardValues();
    void ReadMouseValues();
    void ReadTouchscreenValues();

    void SaveValues();
    void SavePlayerValues();
    void SaveDebugValues();
    void SaveMouseValues();
    void SaveTouchscreenValues();

    std::unique_ptr<QSettings> qt_config;
    std::string qt_config_loc;
};
