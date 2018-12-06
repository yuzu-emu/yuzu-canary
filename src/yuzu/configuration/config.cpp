// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QSettings>
#include "common/file_util.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/hle/service/hid/controllers/npad.h"
#include "input_common/main.h"
#include "yuzu/configuration/config.h"
#include "yuzu/ui_settings.h"

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

const std::array<int, Settings::NativeMouseButton::NumMouseButtons> Config::default_mouse_buttons =
    {
        Qt::Key_BracketLeft, Qt::Key_BracketRight, Qt::Key_Apostrophe, Qt::Key_Minus, Qt::Key_Equal,
};

const std::array<int, Settings::NativeKeyboard::NumKeyboardKeys> Config::default_keyboard_keys = {
    0,
    0,
    0,
    0,
    Qt::Key_A,
    Qt::Key_B,
    Qt::Key_C,
    Qt::Key_D,
    Qt::Key_E,
    Qt::Key_F,
    Qt::Key_G,
    Qt::Key_H,
    Qt::Key_I,
    Qt::Key_J,
    Qt::Key_K,
    Qt::Key_L,
    Qt::Key_M,
    Qt::Key_N,
    Qt::Key_O,
    Qt::Key_P,
    Qt::Key_Q,
    Qt::Key_R,
    Qt::Key_S,
    Qt::Key_T,
    Qt::Key_U,
    Qt::Key_V,
    Qt::Key_W,
    Qt::Key_X,
    Qt::Key_Y,
    Qt::Key_Z,
    Qt::Key_1,
    Qt::Key_2,
    Qt::Key_3,
    Qt::Key_4,
    Qt::Key_5,
    Qt::Key_6,
    Qt::Key_7,
    Qt::Key_8,
    Qt::Key_9,
    Qt::Key_0,
    Qt::Key_Enter,
    Qt::Key_Escape,
    Qt::Key_Backspace,
    Qt::Key_Tab,
    Qt::Key_Space,
    Qt::Key_Minus,
    Qt::Key_Equal,
    Qt::Key_BracketLeft,
    Qt::Key_BracketRight,
    Qt::Key_Backslash,
    Qt::Key_Dead_Tilde,
    Qt::Key_Semicolon,
    Qt::Key_Apostrophe,
    Qt::Key_Dead_Grave,
    Qt::Key_Comma,
    Qt::Key_Period,
    Qt::Key_Slash,
    Qt::Key_CapsLock,

    Qt::Key_F1,
    Qt::Key_F2,
    Qt::Key_F3,
    Qt::Key_F4,
    Qt::Key_F5,
    Qt::Key_F6,
    Qt::Key_F7,
    Qt::Key_F8,
    Qt::Key_F9,
    Qt::Key_F10,
    Qt::Key_F11,
    Qt::Key_F12,

    Qt::Key_SysReq,
    Qt::Key_ScrollLock,
    Qt::Key_Pause,
    Qt::Key_Insert,
    Qt::Key_Home,
    Qt::Key_PageUp,
    Qt::Key_Delete,
    Qt::Key_End,
    Qt::Key_PageDown,
    Qt::Key_Right,
    Qt::Key_Left,
    Qt::Key_Down,
    Qt::Key_Up,

    Qt::Key_NumLock,
    Qt::Key_Slash,
    Qt::Key_Asterisk,
    Qt::Key_Minus,
    Qt::Key_Plus,
    Qt::Key_Enter,
    Qt::Key_1,
    Qt::Key_2,
    Qt::Key_3,
    Qt::Key_4,
    Qt::Key_5,
    Qt::Key_6,
    Qt::Key_7,
    Qt::Key_8,
    Qt::Key_9,
    Qt::Key_0,
    Qt::Key_Period,

    0,
    0,
    Qt::Key_PowerOff,
    Qt::Key_Equal,

    Qt::Key_F13,
    Qt::Key_F14,
    Qt::Key_F15,
    Qt::Key_F16,
    Qt::Key_F17,
    Qt::Key_F18,
    Qt::Key_F19,
    Qt::Key_F20,
    Qt::Key_F21,
    Qt::Key_F22,
    Qt::Key_F23,
    Qt::Key_F24,

    Qt::Key_Open,
    Qt::Key_Help,
    Qt::Key_Menu,
    0,
    Qt::Key_Stop,
    Qt::Key_AudioRepeat,
    Qt::Key_Undo,
    Qt::Key_Cut,
    Qt::Key_Copy,
    Qt::Key_Paste,
    Qt::Key_Find,
    Qt::Key_VolumeMute,
    Qt::Key_VolumeUp,
    Qt::Key_VolumeDown,
    Qt::Key_CapsLock,
    Qt::Key_NumLock,
    Qt::Key_ScrollLock,
    Qt::Key_Comma,

    Qt::Key_ParenLeft,
    Qt::Key_ParenRight,
};

const std::array<int, Settings::NativeKeyboard::NumKeyboardMods> Config::default_keyboard_mods = {
    Qt::Key_Control, Qt::Key_Shift, Qt::Key_Alt,   Qt::Key_ApplicationLeft,
    Qt::Key_Control, Qt::Key_Shift, Qt::Key_AltGr, Qt::Key_ApplicationRight,
};

void Config::ReadPlayerValues() {
    for (std::size_t p = 0; p < Settings::values.players.size(); ++p) {
        Settings::values.players[p].connected =
            qt_config->value(QString("player_%1_connected").arg(p), false).toBool();

        Settings::values.players[p].type = static_cast<Settings::ControllerType>(
            qt_config
                ->value(QString("player_%1_type").arg(p),
                        static_cast<u8>(Settings::ControllerType::DualJoycon))
                .toUInt());

        Settings::values.players[p].body_color_left =
            qt_config
                ->value(QString("player_%1_body_color_left").arg(p),
                        Settings::JOYCON_BODY_NEON_BLUE)
                .toUInt();
        Settings::values.players[p].body_color_right =
            qt_config
                ->value(QString("player_%1_body_color_right").arg(p),
                        Settings::JOYCON_BODY_NEON_RED)
                .toUInt();
        Settings::values.players[p].button_color_left =
            qt_config
                ->value(QString("player_%1_button_color_left").arg(p),
                        Settings::JOYCON_BUTTONS_NEON_BLUE)
                .toUInt();
        Settings::values.players[p].button_color_right =
            qt_config
                ->value(QString("player_%1_button_color_right").arg(p),
                        Settings::JOYCON_BUTTONS_NEON_RED)
                .toUInt();

        for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
            std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
            Settings::values.players[p].buttons[i] =
                qt_config
                    ->value(QString("player_%1_").arg(p) + Settings::NativeButton::mapping[i],
                            QString::fromStdString(default_param))
                    .toString()
                    .toStdString();
            if (Settings::values.players[p].buttons[i].empty())
                Settings::values.players[p].buttons[i] = default_param;
        }

        for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
            std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
                default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
                default_analogs[i][3], default_analogs[i][4], 0.5f);
            Settings::values.players[p].analogs[i] =
                qt_config
                    ->value(QString("player_%1_").arg(p) + Settings::NativeAnalog::mapping[i],
                            QString::fromStdString(default_param))
                    .toString()
                    .toStdString();
            if (Settings::values.players[p].analogs[i].empty())
                Settings::values.players[p].analogs[i] = default_param;
        }
    }

    std::stable_partition(
        Settings::values.players.begin(),
        Settings::values.players.begin() +
            Service::HID::Controller_NPad::NPadIdToIndex(Service::HID::NPAD_HANDHELD),
        [](const auto& player) { return player.connected; });
}

void Config::ReadDebugValues() {
    Settings::values.debug_pad_enabled = qt_config->value("debug_pad_enabled", false).toBool();
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
        Settings::values.debug_pad_buttons[i] =
            qt_config
                ->value(QString("debug_pad_") + Settings::NativeButton::mapping[i],
                        QString::fromStdString(default_param))
                .toString()
                .toStdString();
        if (Settings::values.debug_pad_buttons[i].empty())
            Settings::values.debug_pad_buttons[i] = default_param;
    }

    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
            default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
            default_analogs[i][3], default_analogs[i][4], 0.5f);
        Settings::values.debug_pad_analogs[i] =
            qt_config
                ->value(QString("debug_pad_") + Settings::NativeAnalog::mapping[i],
                        QString::fromStdString(default_param))
                .toString()
                .toStdString();
        if (Settings::values.debug_pad_analogs[i].empty())
            Settings::values.debug_pad_analogs[i] = default_param;
    }
}

void Config::ReadKeyboardValues() {
    Settings::values.keyboard_enabled = qt_config->value("keyboard_enabled", false).toBool();

    std::transform(default_keyboard_keys.begin(), default_keyboard_keys.end(),
                   Settings::values.keyboard_keys.begin(), InputCommon::GenerateKeyboardParam);
    std::transform(default_keyboard_mods.begin(), default_keyboard_mods.end(),
                   Settings::values.keyboard_keys.begin() +
                       Settings::NativeKeyboard::LeftControlKey,
                   InputCommon::GenerateKeyboardParam);
    std::transform(default_keyboard_mods.begin(), default_keyboard_mods.end(),
                   Settings::values.keyboard_mods.begin(), InputCommon::GenerateKeyboardParam);
}

void Config::ReadMouseValues() {
    Settings::values.mouse_enabled = qt_config->value("mouse_enabled", false).toBool();

    for (int i = 0; i < Settings::NativeMouseButton::NumMouseButtons; ++i) {
        std::string default_param = InputCommon::GenerateKeyboardParam(default_mouse_buttons[i]);
        Settings::values.mouse_buttons[i] =
            qt_config
                ->value(QString("mouse_") + Settings::NativeMouseButton::mapping[i],
                        QString::fromStdString(default_param))
                .toString()
                .toStdString();
        if (Settings::values.mouse_buttons[i].empty())
            Settings::values.mouse_buttons[i] = default_param;
    }
}

void Config::ReadTouchscreenValues() {
    Settings::values.touchscreen.enabled = qt_config->value("touchscreen_enabled", true).toBool();
    Settings::values.touchscreen.device =
        qt_config->value("touchscreen_device", "engine:emu_window").toString().toStdString();

    Settings::values.touchscreen.finger = qt_config->value("touchscreen_finger", 0).toUInt();
    Settings::values.touchscreen.rotation_angle = qt_config->value("touchscreen_angle", 0).toUInt();
    Settings::values.touchscreen.diameter_x =
        qt_config->value("touchscreen_diameter_x", 15).toUInt();
    Settings::values.touchscreen.diameter_y =
        qt_config->value("touchscreen_diameter_y", 15).toUInt();
    qt_config->endGroup();
}

void Config::ReadValues() {
    qt_config->beginGroup("Controls");

    ReadPlayerValues();
    ReadDebugValues();
    ReadKeyboardValues();
    ReadMouseValues();
    ReadTouchscreenValues();

    Settings::values.motion_device =
        qt_config->value("motion_device", "engine:motion_emu,update_period:100,sensitivity:0.01")
            .toString()
            .toStdString();

    qt_config->beginGroup("Core");
    Settings::values.use_cpu_jit = qt_config->value("use_cpu_jit", true).toBool();
    Settings::values.use_multi_core = qt_config->value("use_multi_core", false).toBool();
    qt_config->endGroup();

    qt_config->beginGroup("Renderer");
    Settings::values.resolution_factor = qt_config->value("resolution_factor", 1.0).toFloat();
    Settings::values.use_frame_limit = qt_config->value("use_frame_limit", true).toBool();
    Settings::values.frame_limit = qt_config->value("frame_limit", 100).toInt();
    Settings::values.use_accurate_gpu_emulation =
        qt_config->value("use_accurate_gpu_emulation", false).toBool();

    Settings::values.bg_red = qt_config->value("bg_red", 0.0).toFloat();
    Settings::values.bg_green = qt_config->value("bg_green", 0.0).toFloat();
    Settings::values.bg_blue = qt_config->value("bg_blue", 0.0).toFloat();
    qt_config->endGroup();

    qt_config->beginGroup("Audio");
    Settings::values.sink_id = qt_config->value("output_engine", "auto").toString().toStdString();
    Settings::values.enable_audio_stretching =
        qt_config->value("enable_audio_stretching", true).toBool();
    Settings::values.audio_device_id =
        qt_config->value("output_device", "auto").toString().toStdString();
    Settings::values.volume = qt_config->value("volume", 1).toFloat();
    qt_config->endGroup();

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
    Settings::values.use_docked_mode = qt_config->value("use_docked_mode", false).toBool();
    Settings::values.enable_nfc = qt_config->value("enable_nfc", true).toBool();

    Settings::values.current_user = std::clamp<int>(qt_config->value("current_user", 0).toInt(), 0,
                                                    Service::Account::MAX_USERS - 1);

    Settings::values.language_index = qt_config->value("language_index", 1).toInt();

    const auto enabled = qt_config->value("rng_seed_enabled", false).toBool();
    if (enabled) {
        Settings::values.rng_seed = qt_config->value("rng_seed", 0).toULongLong();
    } else {
        Settings::values.rng_seed = std::nullopt;
    }

    qt_config->endGroup();

    qt_config->beginGroup("Miscellaneous");
    Settings::values.log_filter = qt_config->value("log_filter", "*:Info").toString().toStdString();
    Settings::values.use_dev_keys = qt_config->value("use_dev_keys", false).toBool();
    qt_config->endGroup();

    qt_config->beginGroup("Debugging");
    Settings::values.use_gdbstub = qt_config->value("use_gdbstub", false).toBool();
    Settings::values.gdbstub_port = qt_config->value("gdbstub_port", 24689).toInt();
    Settings::values.program_args = qt_config->value("program_args", "").toString().toStdString();
    Settings::values.dump_exefs = qt_config->value("dump_exefs", false).toBool();
    Settings::values.dump_nso = qt_config->value("dump_nso", false).toBool();
    qt_config->endGroup();

    qt_config->beginGroup("WebService");
    Settings::values.enable_telemetry = qt_config->value("enable_telemetry", true).toBool();
    Settings::values.web_api_url =
        qt_config->value("web_api_url", "https://api.yuzu-emu.org").toString().toStdString();
    Settings::values.yuzu_username = qt_config->value("yuzu_username").toString().toStdString();
    Settings::values.yuzu_token = qt_config->value("yuzu_token").toString().toStdString();
    qt_config->endGroup();

    const auto size = qt_config->beginReadArray("DisabledAddOns");
    for (int i = 0; i < size; ++i) {
        qt_config->setArrayIndex(i);
        const auto title_id = qt_config->value("title_id", 0).toULongLong();
        std::vector<std::string> out;
        const auto d_size = qt_config->beginReadArray("disabled");
        for (int j = 0; j < d_size; ++j) {
            qt_config->setArrayIndex(j);
            out.push_back(qt_config->value("d", "").toString().toStdString());
        }
        qt_config->endArray();
        Settings::values.disabled_addons.insert_or_assign(title_id, out);
    }
    qt_config->endArray();

    qt_config->beginGroup("UI");
    UISettings::values.theme = qt_config->value("theme", UISettings::themes[0].second).toString();
    UISettings::values.enable_discord_presence =
        qt_config->value("enable_discord_presence", true).toBool();

    qt_config->beginGroup("UIGameList");
    UISettings::values.show_unknown = qt_config->value("show_unknown", true).toBool();
    UISettings::values.show_add_ons = qt_config->value("show_add_ons", true).toBool();
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

void Config::SavePlayerValues() {
    for (int p = 0; p < Settings::values.players.size(); ++p) {
        qt_config->setValue(QString("player_%1_connected").arg(p),
                            Settings::values.players[p].connected);
        qt_config->setValue(QString("player_%1_type").arg(p),
                            static_cast<u8>(Settings::values.players[p].type));

        qt_config->setValue(QString("player_%1_body_color_left").arg(p),
                            Settings::values.players[p].body_color_left);
        qt_config->setValue(QString("player_%1_body_color_right").arg(p),
                            Settings::values.players[p].body_color_right);
        qt_config->setValue(QString("player_%1_button_color_left").arg(p),
                            Settings::values.players[p].button_color_left);
        qt_config->setValue(QString("player_%1_button_color_right").arg(p),
                            Settings::values.players[p].button_color_right);

        for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
            qt_config->setValue(QString("player_%1_").arg(p) +
                                    QString::fromStdString(Settings::NativeButton::mapping[i]),
                                QString::fromStdString(Settings::values.players[p].buttons[i]));
        }
        for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
            qt_config->setValue(QString("player_%1_").arg(p) +
                                    QString::fromStdString(Settings::NativeAnalog::mapping[i]),
                                QString::fromStdString(Settings::values.players[p].analogs[i]));
        }
    }
}

void Config::SaveDebugValues() {
    qt_config->setValue("debug_pad_enabled", Settings::values.debug_pad_enabled);
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        qt_config->setValue(QString("debug_pad_") +
                                QString::fromStdString(Settings::NativeButton::mapping[i]),
                            QString::fromStdString(Settings::values.debug_pad_buttons[i]));
    }
    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        qt_config->setValue(QString("debug_pad_") +
                                QString::fromStdString(Settings::NativeAnalog::mapping[i]),
                            QString::fromStdString(Settings::values.debug_pad_analogs[i]));
    }
}

void Config::SaveMouseValues() {
    qt_config->setValue("mouse_enabled", Settings::values.mouse_enabled);

    for (int i = 0; i < Settings::NativeMouseButton::NumMouseButtons; ++i) {
        qt_config->setValue(QString("mouse_") +
                                QString::fromStdString(Settings::NativeMouseButton::mapping[i]),
                            QString::fromStdString(Settings::values.mouse_buttons[i]));
    }
}

void Config::SaveTouchscreenValues() {
    qt_config->setValue("touchscreen_enabled", Settings::values.touchscreen.enabled);
    qt_config->setValue("touchscreen_device",
                        QString::fromStdString(Settings::values.touchscreen.device));

    qt_config->setValue("touchscreen_finger", Settings::values.touchscreen.finger);
    qt_config->setValue("touchscreen_angle", Settings::values.touchscreen.rotation_angle);
    qt_config->setValue("touchscreen_diameter_x", Settings::values.touchscreen.diameter_x);
    qt_config->setValue("touchscreen_diameter_y", Settings::values.touchscreen.diameter_y);
}

void Config::SaveValues() {
    qt_config->beginGroup("Controls");

    SavePlayerValues();
    SaveDebugValues();
    SaveMouseValues();
    SaveTouchscreenValues();

    qt_config->setValue("motion_device", QString::fromStdString(Settings::values.motion_device));
    qt_config->setValue("keyboard_enabled", Settings::values.keyboard_enabled);

    qt_config->endGroup();

    qt_config->beginGroup("Core");
    qt_config->setValue("use_cpu_jit", Settings::values.use_cpu_jit);
    qt_config->setValue("use_multi_core", Settings::values.use_multi_core);
    qt_config->endGroup();

    qt_config->beginGroup("Renderer");
    qt_config->setValue("resolution_factor", (double)Settings::values.resolution_factor);
    qt_config->setValue("use_frame_limit", Settings::values.use_frame_limit);
    qt_config->setValue("frame_limit", Settings::values.frame_limit);
    qt_config->setValue("use_accurate_gpu_emulation", Settings::values.use_accurate_gpu_emulation);

    // Cast to double because Qt's written float values are not human-readable
    qt_config->setValue("bg_red", (double)Settings::values.bg_red);
    qt_config->setValue("bg_green", (double)Settings::values.bg_green);
    qt_config->setValue("bg_blue", (double)Settings::values.bg_blue);
    qt_config->endGroup();

    qt_config->beginGroup("Audio");
    qt_config->setValue("output_engine", QString::fromStdString(Settings::values.sink_id));
    qt_config->setValue("enable_audio_stretching", Settings::values.enable_audio_stretching);
    qt_config->setValue("output_device", QString::fromStdString(Settings::values.audio_device_id));
    qt_config->setValue("volume", Settings::values.volume);
    qt_config->endGroup();

    qt_config->beginGroup("Data Storage");
    qt_config->setValue("use_virtual_sd", Settings::values.use_virtual_sd);
    qt_config->setValue("nand_directory",
                        QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::NANDDir)));
    qt_config->setValue("sdmc_directory",
                        QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir)));
    qt_config->endGroup();

    qt_config->beginGroup("System");
    qt_config->setValue("use_docked_mode", Settings::values.use_docked_mode);
    qt_config->setValue("enable_nfc", Settings::values.enable_nfc);
    qt_config->setValue("current_user", Settings::values.current_user);
    qt_config->setValue("language_index", Settings::values.language_index);

    qt_config->setValue("rng_seed_enabled", Settings::values.rng_seed.has_value());
    qt_config->setValue("rng_seed", Settings::values.rng_seed.value_or(0));

    qt_config->endGroup();

    qt_config->beginGroup("Miscellaneous");
    qt_config->setValue("log_filter", QString::fromStdString(Settings::values.log_filter));
    qt_config->setValue("use_dev_keys", Settings::values.use_dev_keys);
    qt_config->endGroup();

    qt_config->beginGroup("Debugging");
    qt_config->setValue("use_gdbstub", Settings::values.use_gdbstub);
    qt_config->setValue("gdbstub_port", Settings::values.gdbstub_port);
    qt_config->setValue("program_args", QString::fromStdString(Settings::values.program_args));
    qt_config->setValue("dump_exefs", Settings::values.dump_exefs);
    qt_config->setValue("dump_nso", Settings::values.dump_nso);
    qt_config->endGroup();

    qt_config->beginGroup("WebService");
    qt_config->setValue("enable_telemetry", Settings::values.enable_telemetry);
    qt_config->setValue("web_api_url", QString::fromStdString(Settings::values.web_api_url));
    qt_config->setValue("yuzu_username", QString::fromStdString(Settings::values.yuzu_username));
    qt_config->setValue("yuzu_token", QString::fromStdString(Settings::values.yuzu_token));
    qt_config->endGroup();

    qt_config->beginWriteArray("DisabledAddOns");
    int i = 0;
    for (const auto& elem : Settings::values.disabled_addons) {
        qt_config->setArrayIndex(i);
        qt_config->setValue("title_id", QVariant::fromValue<u64>(elem.first));
        qt_config->beginWriteArray("disabled");
        for (std::size_t j = 0; j < elem.second.size(); ++j) {
            qt_config->setArrayIndex(j);
            qt_config->setValue("d", QString::fromStdString(elem.second[j]));
        }
        qt_config->endArray();
        ++i;
    }
    qt_config->endArray();

    qt_config->beginGroup("UI");
    qt_config->setValue("theme", UISettings::values.theme);
    qt_config->setValue("enable_discord_presence", UISettings::values.enable_discord_presence);

    qt_config->beginGroup("UIGameList");
    qt_config->setValue("show_unknown", UISettings::values.show_unknown);
    qt_config->setValue("show_add_ons", UISettings::values.show_add_ons);
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
