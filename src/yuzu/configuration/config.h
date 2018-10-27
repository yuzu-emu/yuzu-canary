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

struct PerGameValuesChange {
    // Controls
    std::array<bool, Settings::NativeButton::NumButtons> buttons;
    std::array<bool, Settings::NativeAnalog::NumAnalogs> analogs;
    bool motion_device;
    bool touch_device;

    // System
    bool use_docked_mode;

    // Renderer
    bool resolution_factor;
    bool use_frame_limit;
    bool frame_limit;

    bool bg_red;
    bool bg_green;
    bool bg_blue;

    // Audio
    bool sink_id;
    bool enable_audio_stretching;
    bool audio_device_id;
    bool volume;

    // Debugging
    bool program_args;
};

PerGameValuesChange CalculateValuesDelta(const Settings::PerGameValues& base,
                                         const Settings::PerGameValues& changed);

Settings::PerGameValues ApplyValuesDelta(const Settings::PerGameValues& base,
                                         const Settings::PerGameValues& changed,
                                         const PerGameValuesChange& changes);

class Config {
public:
    Config();
    ~Config();

    void Reload();
    void Save();
    PerGameValuesChange GetPerGameSettingsDelta(u64 title_id) const;
    void SetPerGameSettingsDelta(u64 title_id, PerGameValuesChange change);

    static const std::array<int, Settings::NativeButton::NumButtons> default_buttons;
    static const std::array<std::array<int, 5>, Settings::NativeAnalog::NumAnalogs> default_analogs;

private:
    void ReadValues();
    void SaveValues();

    void ReadPerGameSettings(Settings::PerGameValues& values) const;
    void ReadPerGameSettingsDelta(PerGameValuesChange& values) const;
    void SavePerGameSettings(const Settings::PerGameValues& values);
    void SavePerGameSettingsDelta(const PerGameValuesChange& values);

    bool UpdateCurrentGame(u64 title_id, Settings::PerGameValues& values);

    std::unique_ptr<QSettings> qt_config;
    std::string qt_config_loc;
    std::map<u64, Settings::PerGameValues> update_values;
    std::map<u64, PerGameValuesChange> update_values_delta;
};
