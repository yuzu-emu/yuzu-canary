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
    void ApplyDefaultProfileIfInputInvalid();

    // Read functions bases off the respective config section names.
    void ReadAudioValues();
    void ReadControlValues();
    void ReadCoreValues();
    void ReadDataStorageValues();
    void ReadDebuggingValues();
    void ReadServiceValues();
    void ReadDisabledAddOnValues();
    void ReadMiscellaneousValues();
    void ReadPathValues();
    void ReadRendererValues();
    void ReadShortcutValues();
    void ReadSystemValues();
    void ReadUIValues();
    void ReadUIGamelistValues();
    void ReadUILayoutValues();
    void ReadWebServiceValues();

    void SaveValues();
    void SavePlayerValues();
    void SaveDebugValues();
    void SaveMouseValues();
    void SaveTouchscreenValues();

    // Save functions based off the respective config section names.
    void SaveAudioValues();
    void SaveControlValues();
    void SaveCoreValues();
    void SaveDataStorageValues();
    void SaveDebuggingValues();
    void SaveServiceValues();
    void SaveDisabledAddOnValues();
    void SaveMiscellaneousValues();
    void SavePathValues();
    void SaveRendererValues();
    void SaveShortcutValues();
    void SaveSystemValues();
    void SaveUIValues();
    void SaveUIGamelistValues();
    void SaveUILayoutValues();
    void SaveWebServiceValues();

    QVariant ReadSetting(const QString& name) const;
    QVariant ReadSetting(const QString& name, const QVariant& default_value) const;
    void WriteSetting(const QString& name, const QVariant& value);
    void WriteSetting(const QString& name, const QVariant& value, const QVariant& default_value);

    std::unique_ptr<QSettings> qt_config;
    std::string qt_config_loc;
};
