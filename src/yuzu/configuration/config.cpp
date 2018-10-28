// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QSettings>
#include "common/file_util.h"
#include "core/hle/service/acc/profile_manager.h"
#include "input_common/main.h"
#include "yuzu/configuration/config.h"
#include "yuzu/ui_settings.h"

PerGameValuesChange CalculateValuesDelta(const Settings::PerGameValues& base,
                                         const Settings::PerGameValues& changed) {
    PerGameValuesChange out{};
    for (std::size_t i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        if (base.buttons[i] != changed.buttons[i])
            out.buttons[i] = true;
    }

    for (std::size_t i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        if (base.analogs[i] != changed.analogs[i])
            out.analogs[i] = true;
    }

    out.motion_device = base.motion_device != changed.motion_device;
    out.touch_device = base.touch_device != changed.touch_device;

    out.use_docked_mode = base.use_docked_mode != changed.use_docked_mode;

    out.resolution_factor = base.resolution_factor != changed.resolution_factor;
    out.use_frame_limit = base.use_frame_limit != changed.use_frame_limit;
    out.frame_limit = base.frame_limit != changed.frame_limit;

    out.bg_red = base.bg_red != changed.bg_red;
    out.bg_green = base.bg_green != changed.bg_green;
    out.bg_blue = base.bg_blue != changed.bg_blue;

    out.sink_id = base.sink_id != changed.sink_id;
    out.enable_audio_stretching = base.enable_audio_stretching != changed.enable_audio_stretching;
    out.audio_device_id = base.audio_device_id != changed.audio_device_id;
    out.volume = base.volume != changed.volume;

    out.program_args = base.program_args != changed.program_args;

    return out;
}

Config::Config() {
    // TODO: Don't hardcode the path; let the frontend decide where to put the config files.
    qt_config_loc = FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) + "qt-config.ini";
    FileUtil::CreateFullPath(qt_config_loc);
    qt_config =
        std::make_unique<QSettings>(QString::fromStdString(qt_config_loc), QSettings::IniFormat);

    Reload();
}

Config::~Config() {
    Save();
}

const std::array<int, Settings::NativeButton::NumButtons> Config::default_buttons = {
    Qt::Key_A, Qt::Key_S, Qt::Key_Z,    Qt::Key_X,  Qt::Key_3,     Qt::Key_4,    Qt::Key_Q,
    Qt::Key_W, Qt::Key_1, Qt::Key_2,    Qt::Key_N,  Qt::Key_M,     Qt::Key_F,    Qt::Key_T,
    Qt::Key_H, Qt::Key_G, Qt::Key_Left, Qt::Key_Up, Qt::Key_Right, Qt::Key_Down, Qt::Key_J,
    Qt::Key_I, Qt::Key_L, Qt::Key_K,    Qt::Key_D,  Qt::Key_C,     Qt::Key_B,    Qt::Key_V,
};

const std::array<std::array<int, 5>, Settings::NativeAnalog::NumAnalogs> Config::default_analogs{{
    {
        Qt::Key_Up,
        Qt::Key_Down,
        Qt::Key_Left,
        Qt::Key_Right,
        Qt::Key_E,
    },
    {
        Qt::Key_I,
        Qt::Key_K,
        Qt::Key_J,
        Qt::Key_L,
        Qt::Key_R,
    },
}};

Settings::PerGameValues ApplyValuesDelta(const Settings::PerGameValues& base,
                                         const Settings::PerGameValues& changed,
                                         const PerGameValuesChange& changes) {
    Settings::PerGameValues out{};
    for (std::size_t i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        out.buttons[i] = changes.buttons[i] ? changed.buttons[i] : base.buttons[i];
    }

    for (std::size_t i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        out.analogs[i] = changes.analogs[i] ? changed.analogs[i] : base.analogs[i];
    }

    out.motion_device = changes.motion_device ? changed.motion_device : base.motion_device;
    out.touch_device = changes.touch_device ? changed.touch_device : base.touch_device;

    out.use_docked_mode = changes.use_docked_mode ? changed.use_docked_mode : base.use_docked_mode;

    out.resolution_factor =
        changes.resolution_factor ? changed.resolution_factor : base.resolution_factor;
    out.use_frame_limit = changes.use_frame_limit ? changed.use_frame_limit : base.use_frame_limit;
    out.frame_limit = changes.frame_limit ? changed.frame_limit : base.frame_limit;

    out.bg_red = changes.bg_red ? changed.bg_red : base.bg_red;
    out.bg_green = changes.bg_green ? changed.bg_green : base.bg_green;
    out.bg_blue = changes.bg_blue ? changed.bg_blue : base.bg_blue;

    out.sink_id = changes.sink_id ? changed.sink_id : base.sink_id;
    out.enable_audio_stretching = changes.enable_audio_stretching ? changed.enable_audio_stretching
                                                                  : base.enable_audio_stretching;
    out.audio_device_id = changes.audio_device_id ? changed.audio_device_id : base.audio_device_id;
    out.volume = changes.volume ? changed.volume : base.volume;

    out.program_args = changes.program_args ? changed.program_args : base.program_args;

    out.disabled_patches = changed.disabled_patches;

    return out;
}

void Config::ReadPerGameSettings(Settings::PerGameValues& values) const {
    qt_config->beginGroup("Controls");
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
        values.buttons[i] =
            qt_config
                ->value(Settings::NativeButton::mapping[i], QString::fromStdString(default_param))
                .toString()
                .toStdString();
        if (values.buttons[i].empty())
            values.buttons[i] = default_param;
    }

    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
            default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
            default_analogs[i][3], default_analogs[i][4], 0.5f);
        values.analogs[i] =
            qt_config
                ->value(Settings::NativeAnalog::mapping[i], QString::fromStdString(default_param))
                .toString()
                .toStdString();
        if (values.analogs[i].empty())
            values.analogs[i] = default_param;
    }

    values.motion_device =
        qt_config->value("motion_device", "engine:motion_emu,update_period:100,sensitivity:0.01")
            .toString()
            .toStdString();
    values.touch_device =
        qt_config->value("touch_device", "engine:emu_window").toString().toStdString();

    qt_config->endGroup();

    qt_config->beginGroup("System");
    values.use_docked_mode = qt_config->value("use_docked_mode", false).toBool();
    qt_config->endGroup();

    qt_config->beginGroup("Renderer");
    values.resolution_factor = qt_config->value("resolution_factor", 1.0).toFloat();
    values.use_frame_limit = qt_config->value("use_frame_limit", true).toBool();
    values.frame_limit = qt_config->value("frame_limit", 100).toInt();

    values.bg_red = qt_config->value("bg_red", 0.0).toFloat();
    values.bg_green = qt_config->value("bg_green", 0.0).toFloat();
    values.bg_blue = qt_config->value("bg_blue", 0.0).toFloat();
    qt_config->endGroup();

    qt_config->beginGroup("Audio");
    values.sink_id = qt_config->value("output_engine", "auto").toString().toStdString();
    values.enable_audio_stretching = qt_config->value("enable_audio_stretching", true).toBool();
    values.audio_device_id = qt_config->value("output_device", "auto").toString().toStdString();
    values.volume = qt_config->value("volume", 1).toFloat();
    qt_config->endGroup();

    qt_config->beginGroup("Debugging");
    values.program_args = qt_config->value("program_args", "").toString().toStdString();
    qt_config->endGroup();

    qt_config->beginGroup("Add Ons");
    const auto size = qt_config->beginReadArray("Disabled");

    values.disabled_patches = std::vector<std::string>(size);
    for (std::size_t i = 0; i < size; ++i) {
        qt_config->setArrayIndex(i);
        values.disabled_patches[i] = qt_config->value("name", "").toString().toStdString();
    }
    qt_config->endArray();
    qt_config->endGroup();
}

void Config::ReadPerGameSettingsDelta(PerGameValuesChange& values) const {
    qt_config->beginGroup("Controls");
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        values.buttons[i] = qt_config
                                ->value(QString::fromStdString(Settings::NativeButton::mapping[i] +
                                                               std::string("_changed")),
                                        false)
                                .toBool();
    }

    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        values.analogs[i] = qt_config
                                ->value(QString::fromStdString(Settings::NativeAnalog::mapping[i] +
                                                               std::string("_changed")),
                                        false)
                                .toBool();
    }

    values.motion_device = qt_config->value("motion_device_changed", false).toBool();
    values.touch_device = qt_config->value("touch_device_changed", false).toBool();

    qt_config->endGroup();

    qt_config->beginGroup("System");
    values.use_docked_mode = qt_config->value("use_docked_mode_changed", false).toBool();
    qt_config->endGroup();

    qt_config->beginGroup("Renderer");
    values.resolution_factor = qt_config->value("resolution_factor_changed", false).toBool();
    values.use_frame_limit = qt_config->value("use_frame_limit_changed", false).toBool();
    values.frame_limit = qt_config->value("frame_limit_changed", false).toBool();

    values.bg_red = qt_config->value("bg_red_changed", false).toBool();
    values.bg_green = qt_config->value("bg_green_changed", false).toBool();
    values.bg_blue = qt_config->value("bg_blue_changed", false).toBool();
    qt_config->endGroup();

    qt_config->beginGroup("Audio");
    values.sink_id = qt_config->value("output_engine_changed", false).toBool();
    values.enable_audio_stretching =
        qt_config->value("enable_audio_stretching_changed", false).toBool();
    values.audio_device_id = qt_config->value("output_device_changed", false).toBool();
    values.volume = qt_config->value("volume_changed", false).toBool();
    qt_config->endGroup();

    qt_config->beginGroup("Debugging");
    values.program_args = qt_config->value("program_args_changed", false).toBool();
    qt_config->endGroup();
}

bool Config::UpdateCurrentGame(u64 title_id, Settings::PerGameValues& values) {
    if (Settings::values.CurrentTitleID() != 0) {
        update_values.insert_or_assign(Settings::values.CurrentTitleID(), *Settings::values);
    }

    if (update_values.find(title_id) != update_values.end()) {
        values = ApplyValuesDelta(Settings::values.default_game, update_values[title_id],
                                  GetPerGameSettingsDelta(title_id));
        return true;
    }

    const auto size = qt_config->beginReadArray("Per Game Settings");

    bool found = false;
    for (int i = 0; i < size; ++i) {
        qt_config->setArrayIndex(i);
        const auto read_title_id = qt_config->value("title_id", 0).toULongLong();
        if (read_title_id != title_id)
            continue;

        found = true;

        PerGameValuesChange changes{};
        ReadPerGameSettings(values);
        ReadPerGameSettingsDelta(changes);

        values = ApplyValuesDelta(Settings::values.default_game, values, changes);
    }

    qt_config->endArray();

    if (!found)
        values = Settings::values.default_game;

    return true;
}

void Config::ReadValues() {
    ReadPerGameSettings(Settings::values.default_game);
    Settings::values.SetUpdateCurrentGameFunction(
        [this](u64 title_id, Settings::PerGameValues& values) {
            return UpdateCurrentGame(title_id, values);
        });

    qt_config->beginGroup("Data Storage");
    Settings::values.use_virtual_sd = qt_config->value("use_virtual_sd", true).toBool();
    FileUtil::GetUserPath(
        FileUtil::UserPath::NANDDir,
        qt_config
            ->value("nand_directory",
                    QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::NANDDir)))
            .toString()
            .toStdString());
    FileUtil::GetUserPath(
        FileUtil::UserPath::SDMCDir,
        qt_config
            ->value("sdmc_directory",
                    QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir)))
            .toString()
            .toStdString());
    qt_config->endGroup();

    qt_config->beginGroup("Core");
    Settings::values.use_cpu_jit = qt_config->value("use_cpu_jit", true).toBool();
    Settings::values.use_multi_core = qt_config->value("use_multi_core", false).toBool();
    qt_config->endGroup();

    qt_config->beginGroup("System");
    Settings::values.enable_nfc = qt_config->value("enable_nfc", true).toBool();

    Settings::values.current_user = std::clamp<int>(qt_config->value("current_user", 0).toInt(), 0,
                                                    Service::Account::MAX_USERS - 1);

    Settings::values.language_index = qt_config->value("language_index", 1).toInt();
    qt_config->endGroup();

    qt_config->beginGroup("Renderer");
    Settings::values.use_accurate_gpu_emulation =
        qt_config->value("use_accurate_gpu_emulation", false).toBool();
    qt_config->endGroup();

    qt_config->beginGroup("Miscellaneous");
    Settings::values.log_filter = qt_config->value("log_filter", "*:Info").toString().toStdString();
    Settings::values.use_dev_keys = qt_config->value("use_dev_keys", false).toBool();
    qt_config->endGroup();

    qt_config->beginGroup("Debugging");
    Settings::values.use_gdbstub = qt_config->value("use_gdbstub", false).toBool();
    Settings::values.gdbstub_port = qt_config->value("gdbstub_port", 24689).toInt();
    qt_config->endGroup();

    qt_config->beginGroup("WebService");
    Settings::values.enable_telemetry = qt_config->value("enable_telemetry", true).toBool();
    Settings::values.web_api_url =
        qt_config->value("web_api_url", "https://api.yuzu-emu.org").toString().toStdString();
    Settings::values.yuzu_username = qt_config->value("yuzu_username").toString().toStdString();
    Settings::values.yuzu_token = qt_config->value("yuzu_token").toString().toStdString();
    qt_config->endGroup();

    qt_config->beginGroup("UI");
    UISettings::values.theme = qt_config->value("theme", UISettings::themes[0].second).toString();
    UISettings::values.enable_discord_presence =
        qt_config->value("enable_discord_presence", true).toBool();

    qt_config->beginGroup("UIGameList");
    UISettings::values.show_unknown = qt_config->value("show_unknown", true).toBool();
    UISettings::values.icon_size = qt_config->value("icon_size", 64).toUInt();
    UISettings::values.row_1_text_id = qt_config->value("row_1_text_id", 3).toUInt();
    UISettings::values.row_2_text_id = qt_config->value("row_2_text_id", 2).toUInt();
    qt_config->endGroup();

    qt_config->beginGroup("UILayout");
    UISettings::values.geometry = qt_config->value("geometry").toByteArray();
    UISettings::values.state = qt_config->value("state").toByteArray();
    UISettings::values.renderwindow_geometry =
        qt_config->value("geometryRenderWindow").toByteArray();
    UISettings::values.gamelist_header_state =
        qt_config->value("gameListHeaderState").toByteArray();
    UISettings::values.microprofile_geometry =
        qt_config->value("microProfileDialogGeometry").toByteArray();
    UISettings::values.microprofile_visible =
        qt_config->value("microProfileDialogVisible", false).toBool();
    qt_config->endGroup();

    qt_config->beginGroup("Paths");
    UISettings::values.roms_path = qt_config->value("romsPath").toString();
    UISettings::values.symbols_path = qt_config->value("symbolsPath").toString();
    UISettings::values.gamedir = qt_config->value("gameListRootDir", ".").toString();
    UISettings::values.gamedir_deepscan = qt_config->value("gameListDeepScan", false).toBool();
    UISettings::values.recent_files = qt_config->value("recentFiles").toStringList();
    qt_config->endGroup();

    qt_config->beginGroup("Shortcuts");
    QStringList groups = qt_config->childGroups();
    for (auto group : groups) {
        qt_config->beginGroup(group);

        QStringList hotkeys = qt_config->childGroups();
        for (auto hotkey : hotkeys) {
            qt_config->beginGroup(hotkey);
            UISettings::values.shortcuts.emplace_back(UISettings::Shortcut(
                group + "/" + hotkey,
                UISettings::ContextualShortcut(qt_config->value("KeySeq").toString(),
                                               qt_config->value("Context").toInt())));
            qt_config->endGroup();
        }

        qt_config->endGroup();
    }
    qt_config->endGroup();

    UISettings::values.single_window_mode = qt_config->value("singleWindowMode", true).toBool();
    UISettings::values.fullscreen = qt_config->value("fullscreen", false).toBool();
    UISettings::values.display_titlebar = qt_config->value("displayTitleBars", true).toBool();
    UISettings::values.show_filter_bar = qt_config->value("showFilterBar", true).toBool();
    UISettings::values.show_status_bar = qt_config->value("showStatusBar", true).toBool();
    UISettings::values.confirm_before_closing = qt_config->value("confirmClose", true).toBool();
    UISettings::values.first_start = qt_config->value("firstStart", true).toBool();
    UISettings::values.callout_flags = qt_config->value("calloutFlags", 0).toUInt();
    UISettings::values.show_console = qt_config->value("showConsole", false).toBool();

    qt_config->endGroup();
}

void Config::SavePerGameSettings(const Settings::PerGameValues& values) {
    qt_config->beginGroup("Controls");
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        qt_config->setValue(QString::fromStdString(Settings::NativeButton::mapping[i]),
                            QString::fromStdString(values.buttons[i]));
    }
    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        qt_config->setValue(QString::fromStdString(Settings::NativeAnalog::mapping[i]),
                            QString::fromStdString(values.analogs[i]));
    }
    qt_config->setValue("motion_device", QString::fromStdString(values.motion_device));
    qt_config->setValue("touch_device", QString::fromStdString(values.touch_device));
    qt_config->endGroup();

    qt_config->beginGroup("System");
    qt_config->setValue("use_docked_mode", values.use_docked_mode);
    qt_config->endGroup();

    qt_config->beginGroup("Renderer");
    qt_config->setValue("resolution_factor", (double)values.resolution_factor);
    qt_config->setValue("use_frame_limit", values.use_frame_limit);
    qt_config->setValue("frame_limit", values.frame_limit);

    // Cast to double because Qt's written float values are not human-readable
    qt_config->setValue("bg_red", (double)values.bg_red);
    qt_config->setValue("bg_green", (double)values.bg_green);
    qt_config->setValue("bg_blue", (double)values.bg_blue);
    qt_config->endGroup();

    qt_config->beginGroup("Audio");
    qt_config->setValue("output_engine", QString::fromStdString(values.sink_id));
    qt_config->setValue("enable_audio_stretching", values.enable_audio_stretching);
    qt_config->setValue("output_device", QString::fromStdString(values.audio_device_id));
    qt_config->setValue("volume", values.volume);
    qt_config->endGroup();

    qt_config->beginGroup("Debugging");
    qt_config->setValue("program_args", QString::fromStdString(values.program_args));
    qt_config->endGroup();

    qt_config->beginGroup("Add Ons");
    qt_config->beginWriteArray("Disabled", values.disabled_patches.size());
    for (std::size_t i = 0; i < values.disabled_patches.size(); ++i) {
        qt_config->setArrayIndex(i);
        qt_config->setValue("name", QString::fromStdString(values.disabled_patches[i]));
    }
    qt_config->endArray();
    qt_config->endGroup();
}

void Config::SavePerGameSettingsDelta(const PerGameValuesChange& values) {
    qt_config->beginGroup("Controls");
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        qt_config->setValue(QString::fromStdString(Settings::NativeButton::mapping[i]) + "_changed",
                            values.buttons[i]);
    }
    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        qt_config->setValue(QString::fromStdString(Settings::NativeAnalog::mapping[i]) + "_changed",
                            values.analogs[i]);
    }
    qt_config->setValue("motion_device_changed", values.motion_device);
    qt_config->setValue("touch_device_changed", values.touch_device);
    qt_config->endGroup();

    qt_config->beginGroup("System");
    qt_config->setValue("use_docked_mode_changed", values.use_docked_mode);
    qt_config->endGroup();

    qt_config->beginGroup("Renderer");
    qt_config->setValue("resolution_factor_changed", values.resolution_factor);
    qt_config->setValue("use_frame_limit_changed", values.use_frame_limit);
    qt_config->setValue("frame_limit_changed", values.frame_limit);

    qt_config->setValue("bg_red_changed", values.bg_red);
    qt_config->setValue("bg_green_changed", values.bg_green);
    qt_config->setValue("bg_blue_changed", values.bg_blue);
    qt_config->endGroup();

    qt_config->beginGroup("Audio");
    qt_config->setValue("output_engine_changed", values.sink_id);
    qt_config->setValue("enable_audio_stretching_changed", values.enable_audio_stretching);
    qt_config->setValue("output_device_changed", values.audio_device_id);
    qt_config->setValue("volume_changed", values.volume);
    qt_config->endGroup();

    qt_config->beginGroup("Debugging");
    qt_config->setValue("program_args_changed", values.program_args);
    qt_config->endGroup();
}

void Config::SaveValues() {
    SavePerGameSettings(Settings::values.default_game);

    Settings::PerGameValues values;
    UpdateCurrentGame(Settings::values.CurrentTitleID(), values);

    const auto size = qt_config->beginReadArray("Per Game Settings");

    for (int i = 0; i < size; ++i) {
        qt_config->setArrayIndex(i);
        const auto read_title_id = qt_config->value("title_id", 0).toULongLong();
        if (update_values.find(read_title_id) == update_values.end()) {
            ReadPerGameSettings(values);

            PerGameValuesChange change{};
            ReadPerGameSettingsDelta(change);

            update_values.emplace(read_title_id, values);
            update_values_delta.emplace(read_title_id, change);
        }
    }

    qt_config->endArray();

    qt_config->beginWriteArray("Per Game Settings", update_values.size());

    int i = 0;
    for (const auto& kv : update_values) {
        qt_config->setArrayIndex(i++);
        qt_config->setValue("title_id", kv.first);
        SavePerGameSettings(kv.second);

        const auto iter = update_values_delta.find(kv.first);
        if (iter != update_values_delta.end()) {
            SavePerGameSettingsDelta(iter->second);
        } else {
            LOG_WARNING(
                Config,
                "Missing values delta for title_id={:016X}! Falling back to default-derived delta.",
                kv.first);
            SavePerGameSettingsDelta(
                CalculateValuesDelta(Settings::values.default_game, kv.second));
        }
    }

    qt_config->endArray();

    qt_config->beginGroup("Data Storage");
    qt_config->setValue("use_virtual_sd", Settings::values.use_virtual_sd);
    qt_config->setValue("nand_directory",
                        QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::NANDDir)));
    qt_config->setValue("sdmc_directory",
                        QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir)));
    qt_config->endGroup();

    qt_config->beginGroup("Core");
    qt_config->setValue("use_cpu_jit", Settings::values.use_cpu_jit);
    qt_config->setValue("use_multi_core", Settings::values.use_multi_core);
    qt_config->endGroup();

    qt_config->beginGroup("System");
    qt_config->setValue("enable_nfc", Settings::values.enable_nfc);
    qt_config->setValue("current_user", Settings::values.current_user);

    qt_config->setValue("language_index", Settings::values.language_index);
    qt_config->endGroup();

    qt_config->beginGroup("Renderer");
    qt_config->setValue("use_accurate_gpu_emulation", Settings::values.use_accurate_gpu_emulation);
    qt_config->endGroup();

    qt_config->beginGroup("Miscellaneous");
    qt_config->setValue("log_filter", QString::fromStdString(Settings::values.log_filter));
    qt_config->setValue("use_dev_keys", Settings::values.use_dev_keys);
    qt_config->endGroup();

    qt_config->beginGroup("Debugging");
    qt_config->setValue("use_gdbstub", Settings::values.use_gdbstub);
    qt_config->setValue("gdbstub_port", Settings::values.gdbstub_port);
    qt_config->endGroup();

    qt_config->beginGroup("WebService");
    qt_config->setValue("enable_telemetry", Settings::values.enable_telemetry);
    qt_config->setValue("web_api_url", QString::fromStdString(Settings::values.web_api_url));
    qt_config->setValue("yuzu_username", QString::fromStdString(Settings::values.yuzu_username));
    qt_config->setValue("yuzu_token", QString::fromStdString(Settings::values.yuzu_token));
    qt_config->endGroup();

    qt_config->beginGroup("UI");
    qt_config->setValue("theme", UISettings::values.theme);
    qt_config->setValue("enable_discord_presence", UISettings::values.enable_discord_presence);

    qt_config->beginGroup("UIGameList");
    qt_config->setValue("show_unknown", UISettings::values.show_unknown);
    qt_config->setValue("icon_size", UISettings::values.icon_size);
    qt_config->setValue("row_1_text_id", UISettings::values.row_1_text_id);
    qt_config->setValue("row_2_text_id", UISettings::values.row_2_text_id);
    qt_config->endGroup();

    qt_config->beginGroup("UILayout");
    qt_config->setValue("geometry", UISettings::values.geometry);
    qt_config->setValue("state", UISettings::values.state);
    qt_config->setValue("geometryRenderWindow", UISettings::values.renderwindow_geometry);
    qt_config->setValue("gameListHeaderState", UISettings::values.gamelist_header_state);
    qt_config->setValue("microProfileDialogGeometry", UISettings::values.microprofile_geometry);
    qt_config->setValue("microProfileDialogVisible", UISettings::values.microprofile_visible);
    qt_config->endGroup();

    qt_config->beginGroup("Paths");
    qt_config->setValue("romsPath", UISettings::values.roms_path);
    qt_config->setValue("symbolsPath", UISettings::values.symbols_path);
    qt_config->setValue("gameListRootDir", UISettings::values.gamedir);
    qt_config->setValue("gameListDeepScan", UISettings::values.gamedir_deepscan);
    qt_config->setValue("recentFiles", UISettings::values.recent_files);
    qt_config->endGroup();

    qt_config->beginGroup("Shortcuts");
    for (auto shortcut : UISettings::values.shortcuts) {
        qt_config->setValue(shortcut.first + "/KeySeq", shortcut.second.first);
        qt_config->setValue(shortcut.first + "/Context", shortcut.second.second);
    }
    qt_config->endGroup();

    qt_config->setValue("singleWindowMode", UISettings::values.single_window_mode);
    qt_config->setValue("fullscreen", UISettings::values.fullscreen);
    qt_config->setValue("displayTitleBars", UISettings::values.display_titlebar);
    qt_config->setValue("showFilterBar", UISettings::values.show_filter_bar);
    qt_config->setValue("showStatusBar", UISettings::values.show_status_bar);
    qt_config->setValue("confirmClose", UISettings::values.confirm_before_closing);
    qt_config->setValue("firstStart", UISettings::values.first_start);
    qt_config->setValue("calloutFlags", UISettings::values.callout_flags);
    qt_config->setValue("showConsole", UISettings::values.show_console);
    qt_config->endGroup();
}

void Config::Reload() {
    ReadValues();
    Settings::Apply();
}

void Config::Save() {
    SaveValues();
}

PerGameValuesChange Config::GetPerGameSettingsDelta(u64 title_id) const {
    if (update_values_delta.find(title_id) != update_values_delta.end())
        return update_values_delta.at(title_id);

    const auto size = qt_config->beginReadArray("Per Game Settings");

    for (int i = 0; i < size; ++i) {
        qt_config->setArrayIndex(i);
        const auto read_title_id = qt_config->value("title_id", 0).toULongLong();
        if (read_title_id != title_id)
            continue;

        PerGameValuesChange changes{};
        ReadPerGameSettingsDelta(changes);
        qt_config->endArray();
        return changes;
    }

    qt_config->endArray();

    return {};
}

void Config::SetPerGameSettingsDelta(u64 title_id, PerGameValuesChange change) {
    update_values_delta.insert_or_assign(title_id, change);
}
