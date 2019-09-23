// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <QKeySequence>
#include <QSettings>
#include "common/file_util.h"
#include "configure_input_simple.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/hle/service/hid/controllers/npad.h"
#include "input_common/main.h"
#include "yuzu/configuration/config.h"
#include "yuzu/uisettings.h"

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

// This shouldn't have anything except static initializers (no functions). So
// QKeySequence(...).toString() is NOT ALLOWED HERE.
// This must be in alphabetical order according to action name as it must have the same order as
// UISetting::values.shortcuts, which is alphabetically ordered.
// clang-format off
const std::array<UISettings::Shortcut, 15> default_hotkeys{{
    {QStringLiteral("Capture Screenshot"),       QStringLiteral("Main Window"), {QStringLiteral("Ctrl+P"), Qt::ApplicationShortcut}},
    {QStringLiteral("Continue/Pause Emulation"), QStringLiteral("Main Window"), {QStringLiteral("F4"), Qt::WindowShortcut}},
    {QStringLiteral("Decrease Speed Limit"),     QStringLiteral("Main Window"), {QStringLiteral("-"), Qt::ApplicationShortcut}},
    {QStringLiteral("Exit yuzu"),                QStringLiteral("Main Window"), {QStringLiteral("Ctrl+Q"), Qt::WindowShortcut}},
    {QStringLiteral("Exit Fullscreen"),          QStringLiteral("Main Window"), {QStringLiteral("Esc"), Qt::WindowShortcut}},
    {QStringLiteral("Fullscreen"),               QStringLiteral("Main Window"), {QStringLiteral("F11"), Qt::WindowShortcut}},
    {QStringLiteral("Increase Speed Limit"),     QStringLiteral("Main Window"), {QStringLiteral("+"), Qt::ApplicationShortcut}},
    {QStringLiteral("Load Amiibo"),              QStringLiteral("Main Window"), {QStringLiteral("F2"), Qt::ApplicationShortcut}},
    {QStringLiteral("Load File"),                QStringLiteral("Main Window"), {QStringLiteral("Ctrl+O"), Qt::WindowShortcut}},
    {QStringLiteral("Restart Emulation"),        QStringLiteral("Main Window"), {QStringLiteral("F6"), Qt::WindowShortcut}},
    {QStringLiteral("Stop Emulation"),           QStringLiteral("Main Window"), {QStringLiteral("F5"), Qt::WindowShortcut}},
    {QStringLiteral("Toggle Filter Bar"),        QStringLiteral("Main Window"), {QStringLiteral("Ctrl+F"), Qt::WindowShortcut}},
    {QStringLiteral("Toggle Speed Limit"),       QStringLiteral("Main Window"), {QStringLiteral("Ctrl+Z"), Qt::ApplicationShortcut}},
    {QStringLiteral("Toggle Status Bar"),        QStringLiteral("Main Window"), {QStringLiteral("Ctrl+S"), Qt::WindowShortcut}},
    {QStringLiteral("Change Docked Mode"),       QStringLiteral("Main Window"), {QStringLiteral("F10"), Qt::ApplicationShortcut}},
}};
// clang-format on

void Config::ReadPlayerValues() {
    for (std::size_t p = 0; p < Settings::values.players.size(); ++p) {
        auto& player = Settings::values.players[p];

        player.connected =
            ReadSetting(QStringLiteral("player_%1_connected").arg(p), false).toBool();

        player.type = static_cast<Settings::ControllerType>(
            qt_config
                ->value(QStringLiteral("player_%1_type").arg(p),
                        static_cast<u8>(Settings::ControllerType::DualJoycon))
                .toUInt());

        player.body_color_left = qt_config
                                     ->value(QStringLiteral("player_%1_body_color_left").arg(p),
                                             Settings::JOYCON_BODY_NEON_BLUE)
                                     .toUInt();
        player.body_color_right = qt_config
                                      ->value(QStringLiteral("player_%1_body_color_right").arg(p),
                                              Settings::JOYCON_BODY_NEON_RED)
                                      .toUInt();
        player.button_color_left = qt_config
                                       ->value(QStringLiteral("player_%1_button_color_left").arg(p),
                                               Settings::JOYCON_BUTTONS_NEON_BLUE)
                                       .toUInt();
        player.button_color_right =
            qt_config
                ->value(QStringLiteral("player_%1_button_color_right").arg(p),
                        Settings::JOYCON_BUTTONS_NEON_RED)
                .toUInt();

        for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
            const std::string default_param =
                InputCommon::GenerateKeyboardParam(default_buttons[i]);
            auto& player_buttons = player.buttons[i];

            player_buttons = qt_config
                                 ->value(QStringLiteral("player_%1_").arg(p) +
                                             QString::fromUtf8(Settings::NativeButton::mapping[i]),
                                         QString::fromStdString(default_param))
                                 .toString()
                                 .toStdString();
            if (player_buttons.empty()) {
                player_buttons = default_param;
            }
        }

        for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
            const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
                default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
                default_analogs[i][3], default_analogs[i][4], 0.5f);
            auto& player_analogs = player.analogs[i];

            player_analogs = qt_config
                                 ->value(QStringLiteral("player_%1_").arg(p) +
                                             QString::fromUtf8(Settings::NativeAnalog::mapping[i]),
                                         QString::fromStdString(default_param))
                                 .toString()
                                 .toStdString();
            if (player_analogs.empty()) {
                player_analogs = default_param;
            }
        }
    }

    std::stable_partition(
        Settings::values.players.begin(),
        Settings::values.players.begin() +
            Service::HID::Controller_NPad::NPadIdToIndex(Service::HID::NPAD_HANDHELD),
        [](const auto& player) { return player.connected; });
}

void Config::ReadDebugValues() {
    Settings::values.debug_pad_enabled =
        ReadSetting(QStringLiteral("debug_pad_enabled"), false).toBool();

    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        const std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
        auto& debug_pad_buttons = Settings::values.debug_pad_buttons[i];

        debug_pad_buttons = qt_config
                                ->value(QStringLiteral("debug_pad_") +
                                            QString::fromUtf8(Settings::NativeButton::mapping[i]),
                                        QString::fromStdString(default_param))
                                .toString()
                                .toStdString();
        if (debug_pad_buttons.empty()) {
            debug_pad_buttons = default_param;
        }
    }

    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
            default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
            default_analogs[i][3], default_analogs[i][4], 0.5f);
        auto& debug_pad_analogs = Settings::values.debug_pad_analogs[i];

        debug_pad_analogs = qt_config
                                ->value(QStringLiteral("debug_pad_") +
                                            QString::fromUtf8(Settings::NativeAnalog::mapping[i]),
                                        QString::fromStdString(default_param))
                                .toString()
                                .toStdString();
        if (debug_pad_analogs.empty()) {
            debug_pad_analogs = default_param;
        }
    }
}

void Config::ReadKeyboardValues() {
    Settings::values.keyboard_enabled =
        ReadSetting(QStringLiteral("keyboard_enabled"), false).toBool();

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
    Settings::values.mouse_enabled = ReadSetting(QStringLiteral("mouse_enabled"), false).toBool();

    for (int i = 0; i < Settings::NativeMouseButton::NumMouseButtons; ++i) {
        const std::string default_param =
            InputCommon::GenerateKeyboardParam(default_mouse_buttons[i]);
        auto& mouse_buttons = Settings::values.mouse_buttons[i];

        mouse_buttons = qt_config
                            ->value(QStringLiteral("mouse_") +
                                        QString::fromUtf8(Settings::NativeMouseButton::mapping[i]),
                                    QString::fromStdString(default_param))
                            .toString()
                            .toStdString();
        if (mouse_buttons.empty()) {
            mouse_buttons = default_param;
        }
    }
}

void Config::ReadTouchscreenValues() {
    Settings::values.touchscreen.enabled =
        ReadSetting(QStringLiteral("touchscreen_enabled"), true).toBool();
    Settings::values.touchscreen.device =
        ReadSetting(QStringLiteral("touchscreen_device"), QStringLiteral("engine:emu_window"))
            .toString()
            .toStdString();

    Settings::values.touchscreen.finger =
        ReadSetting(QStringLiteral("touchscreen_finger"), 0).toUInt();
    Settings::values.touchscreen.rotation_angle =
        ReadSetting(QStringLiteral("touchscreen_angle"), 0).toUInt();
    Settings::values.touchscreen.diameter_x =
        ReadSetting(QStringLiteral("touchscreen_diameter_x"), 15).toUInt();
    Settings::values.touchscreen.diameter_y =
        ReadSetting(QStringLiteral("touchscreen_diameter_y"), 15).toUInt();
}

void Config::ApplyDefaultProfileIfInputInvalid() {
    if (!std::any_of(Settings::values.players.begin(), Settings::values.players.end(),
                     [](const Settings::PlayerInput& in) { return in.connected; })) {
        ApplyInputProfileConfiguration(UISettings::values.profile_index);
    }
}

void Config::ReadAudioValues() {
    qt_config->beginGroup(QStringLiteral("Audio"));

    Settings::values.sink_id = ReadSetting(QStringLiteral("output_engine"), QStringLiteral("auto"))
                                   .toString()
                                   .toStdString();
    Settings::values.enable_audio_stretching =
        ReadSetting(QStringLiteral("enable_audio_stretching"), true).toBool();
    Settings::values.audio_device_id =
        ReadSetting(QStringLiteral("output_device"), QStringLiteral("auto"))
            .toString()
            .toStdString();
    Settings::values.volume = ReadSetting(QStringLiteral("volume"), 1).toFloat();

    qt_config->endGroup();
}

void Config::ReadControlValues() {
    qt_config->beginGroup(QStringLiteral("Controls"));

    ReadPlayerValues();
    ReadDebugValues();
    ReadKeyboardValues();
    ReadMouseValues();
    ReadTouchscreenValues();

    Settings::values.motion_device =
        ReadSetting(QStringLiteral("motion_device"),
                    QStringLiteral("engine:motion_emu,update_period:100,sensitivity:0.01"))
            .toString()
            .toStdString();

    qt_config->endGroup();
}

void Config::ReadCoreValues() {
    qt_config->beginGroup(QStringLiteral("Core"));

    Settings::values.use_multi_core = ReadSetting(QStringLiteral("use_multi_core"), false).toBool();

    qt_config->endGroup();
}

void Config::ReadDataStorageValues() {
    qt_config->beginGroup(QStringLiteral("Data Storage"));

    Settings::values.use_virtual_sd = ReadSetting(QStringLiteral("use_virtual_sd"), true).toBool();
    FileUtil::GetUserPath(
        FileUtil::UserPath::NANDDir,
        qt_config
            ->value(QStringLiteral("nand_directory"),
                    QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::NANDDir)))
            .toString()
            .toStdString());
    FileUtil::GetUserPath(
        FileUtil::UserPath::SDMCDir,
        qt_config
            ->value(QStringLiteral("sdmc_directory"),
                    QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir)))
            .toString()
            .toStdString());
    FileUtil::GetUserPath(
        FileUtil::UserPath::LoadDir,
        qt_config
            ->value(QStringLiteral("load_directory"),
                    QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::LoadDir)))
            .toString()
            .toStdString());
    FileUtil::GetUserPath(
        FileUtil::UserPath::DumpDir,
        qt_config
            ->value(QStringLiteral("dump_directory"),
                    QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::DumpDir)))
            .toString()
            .toStdString());
    FileUtil::GetUserPath(
        FileUtil::UserPath::CacheDir,
        qt_config
            ->value(QStringLiteral("cache_directory"),
                    QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::CacheDir)))
            .toString()
            .toStdString());
    Settings::values.gamecard_inserted =
        ReadSetting(QStringLiteral("gamecard_inserted"), false).toBool();
    Settings::values.gamecard_current_game =
        ReadSetting(QStringLiteral("gamecard_current_game"), false).toBool();
    Settings::values.gamecard_path =
        ReadSetting(QStringLiteral("gamecard_path"), QStringLiteral("")).toString().toStdString();
    Settings::values.nand_total_size = static_cast<Settings::NANDTotalSize>(
        ReadSetting(QStringLiteral("nand_total_size"),
                    QVariant::fromValue<u64>(static_cast<u64>(Settings::NANDTotalSize::S29_1GB)))
            .toULongLong());
    Settings::values.nand_user_size = static_cast<Settings::NANDUserSize>(
        ReadSetting(QStringLiteral("nand_user_size"),
                    QVariant::fromValue<u64>(static_cast<u64>(Settings::NANDUserSize::S26GB)))
            .toULongLong());
    Settings::values.nand_system_size = static_cast<Settings::NANDSystemSize>(
        ReadSetting(QStringLiteral("nand_system_size"),
                    QVariant::fromValue<u64>(static_cast<u64>(Settings::NANDSystemSize::S2_5GB)))
            .toULongLong());
    Settings::values.sdmc_size = static_cast<Settings::SDMCSize>(
        ReadSetting(QStringLiteral("sdmc_size"),
                    QVariant::fromValue<u64>(static_cast<u64>(Settings::SDMCSize::S16GB)))
            .toULongLong());

    qt_config->endGroup();
}

void Config::ReadDebuggingValues() {
    qt_config->beginGroup(QStringLiteral("Debugging"));

    // Intentionally not using the QT default setting as this is intended to be changed in the ini
    Settings::values.record_frame_times =
        qt_config->value(QStringLiteral("record_frame_times"), false).toBool();
    Settings::values.use_gdbstub = ReadSetting(QStringLiteral("use_gdbstub"), false).toBool();
    Settings::values.gdbstub_port = ReadSetting(QStringLiteral("gdbstub_port"), 24689).toInt();
    Settings::values.program_args =
        ReadSetting(QStringLiteral("program_args"), QStringLiteral("")).toString().toStdString();
    Settings::values.dump_exefs = ReadSetting(QStringLiteral("dump_exefs"), false).toBool();
    Settings::values.dump_nso = ReadSetting(QStringLiteral("dump_nso"), false).toBool();
    Settings::values.reporting_services =
        ReadSetting(QStringLiteral("reporting_services"), false).toBool();
    Settings::values.quest_flag = ReadSetting(QStringLiteral("quest_flag"), false).toBool();

    qt_config->endGroup();
}

void Config::ReadDisabledAddOnValues() {
    const auto size = qt_config->beginReadArray(QStringLiteral("DisabledAddOns"));

    for (int i = 0; i < size; ++i) {
        qt_config->setArrayIndex(i);
        const auto title_id = ReadSetting(QStringLiteral("title_id"), 0).toULongLong();
        std::vector<std::string> out;
        const auto d_size = qt_config->beginReadArray(QStringLiteral("disabled"));
        for (int j = 0; j < d_size; ++j) {
            qt_config->setArrayIndex(j);
            out.push_back(
                ReadSetting(QStringLiteral("d"), QStringLiteral("")).toString().toStdString());
        }
        qt_config->endArray();
        Settings::values.disabled_addons.insert_or_assign(title_id, out);
    }

    qt_config->endArray();
}

void Config::ReadMiscellaneousValues() {
    qt_config->beginGroup(QStringLiteral("Miscellaneous"));

    Settings::values.log_filter =
        ReadSetting(QStringLiteral("log_filter"), QStringLiteral("*:Info"))
            .toString()
            .toStdString();
    Settings::values.use_dev_keys = ReadSetting(QStringLiteral("use_dev_keys"), false).toBool();

    qt_config->endGroup();
}

void Config::ReadPathValues() {
    qt_config->beginGroup(QStringLiteral("Paths"));

    UISettings::values.roms_path = ReadSetting(QStringLiteral("romsPath")).toString();
    UISettings::values.symbols_path = ReadSetting(QStringLiteral("symbolsPath")).toString();
    UISettings::values.screenshot_path = ReadSetting(QStringLiteral("screenshotPath")).toString();
    UISettings::values.game_dir_deprecated =
        ReadSetting(QStringLiteral("gameListRootDir"), QStringLiteral(".")).toString();
    UISettings::values.game_dir_deprecated_deepscan =
        ReadSetting(QStringLiteral("gameListDeepScan"), false).toBool();
    const int gamedirs_size = qt_config->beginReadArray(QStringLiteral("gamedirs"));
    for (int i = 0; i < gamedirs_size; ++i) {
        qt_config->setArrayIndex(i);
        UISettings::GameDir game_dir;
        game_dir.path = ReadSetting(QStringLiteral("path")).toString();
        game_dir.deep_scan = ReadSetting(QStringLiteral("deep_scan"), false).toBool();
        game_dir.expanded = ReadSetting(QStringLiteral("expanded"), true).toBool();
        UISettings::values.game_dirs.append(game_dir);
    }
    qt_config->endArray();
    // create NAND and SD card directories if empty, these are not removable through the UI,
    // also carries over old game list settings if present
    if (UISettings::values.game_dirs.isEmpty()) {
        UISettings::GameDir game_dir;
        game_dir.path = QStringLiteral("SDMC");
        game_dir.expanded = true;
        UISettings::values.game_dirs.append(game_dir);
        game_dir.path = QStringLiteral("UserNAND");
        UISettings::values.game_dirs.append(game_dir);
        game_dir.path = QStringLiteral("SysNAND");
        UISettings::values.game_dirs.append(game_dir);
        if (UISettings::values.game_dir_deprecated != QStringLiteral(".")) {
            game_dir.path = UISettings::values.game_dir_deprecated;
            game_dir.deep_scan = UISettings::values.game_dir_deprecated_deepscan;
            UISettings::values.game_dirs.append(game_dir);
        }
    }
    UISettings::values.recent_files = ReadSetting(QStringLiteral("recentFiles")).toStringList();

    qt_config->endGroup();
}

void Config::ReadRendererValues() {
    qt_config->beginGroup(QStringLiteral("Renderer"));

    Settings::values.resolution_factor =
        ReadSetting(QStringLiteral("resolution_factor"), 1.0).toFloat();
    Settings::values.use_frame_limit =
        ReadSetting(QStringLiteral("use_frame_limit"), true).toBool();
    Settings::values.frame_limit = ReadSetting(QStringLiteral("frame_limit"), 100).toInt();
    Settings::values.use_disk_shader_cache =
        ReadSetting(QStringLiteral("use_disk_shader_cache"), true).toBool();
    Settings::values.use_accurate_gpu_emulation =
        ReadSetting(QStringLiteral("use_accurate_gpu_emulation"), false).toBool();
    Settings::values.use_asynchronous_gpu_emulation =
        ReadSetting(QStringLiteral("use_asynchronous_gpu_emulation"), false).toBool();
    Settings::values.use_resolution_scanner =
        ReadSetting(QStringLiteral("use_resolution_scanner"), false).toBool();
    Settings::values.force_30fps_mode =
        ReadSetting(QStringLiteral("force_30fps_mode"), false).toBool();

    Settings::values.bg_red = ReadSetting(QStringLiteral("bg_red"), 0.0).toFloat();
    Settings::values.bg_green = ReadSetting(QStringLiteral("bg_green"), 0.0).toFloat();
    Settings::values.bg_blue = ReadSetting(QStringLiteral("bg_blue"), 0.0).toFloat();

    qt_config->endGroup();
}

void Config::ReadShortcutValues() {
    qt_config->beginGroup(QStringLiteral("Shortcuts"));

    for (const auto& [name, group, shortcut] : default_hotkeys) {
        const auto& [keyseq, context] = shortcut;
        qt_config->beginGroup(group);
        qt_config->beginGroup(name);
        UISettings::values.shortcuts.push_back(
            {name,
             group,
             {ReadSetting(QStringLiteral("KeySeq"), keyseq).toString(),
              ReadSetting(QStringLiteral("Context"), context).toInt()}});
        qt_config->endGroup();
        qt_config->endGroup();
    }

    qt_config->endGroup();
}

void Config::ReadSystemValues() {
    qt_config->beginGroup(QStringLiteral("System"));

    Settings::values.use_docked_mode =
        ReadSetting(QStringLiteral("use_docked_mode"), false).toBool();

    Settings::values.current_user = std::clamp<int>(
        ReadSetting(QStringLiteral("current_user"), 0).toInt(), 0, Service::Account::MAX_USERS - 1);

    Settings::values.language_index = ReadSetting(QStringLiteral("language_index"), 1).toInt();

    const auto rng_seed_enabled = ReadSetting(QStringLiteral("rng_seed_enabled"), false).toBool();
    if (rng_seed_enabled) {
        Settings::values.rng_seed = ReadSetting(QStringLiteral("rng_seed"), 0).toULongLong();
    } else {
        Settings::values.rng_seed = std::nullopt;
    }

    const auto custom_rtc_enabled =
        ReadSetting(QStringLiteral("custom_rtc_enabled"), false).toBool();
    if (custom_rtc_enabled) {
        Settings::values.custom_rtc =
            std::chrono::seconds(ReadSetting(QStringLiteral("custom_rtc"), 0).toULongLong());
    } else {
        Settings::values.custom_rtc = std::nullopt;
    }

    qt_config->endGroup();
}

void Config::ReadUIValues() {
    qt_config->beginGroup(QStringLiteral("UI"));

    UISettings::values.theme =
        ReadSetting(QStringLiteral("theme"), QString::fromUtf8(UISettings::themes[0].second))
            .toString();
    UISettings::values.enable_discord_presence =
        ReadSetting(QStringLiteral("enable_discord_presence"), true).toBool();
    UISettings::values.screenshot_resolution_factor =
        static_cast<u16>(ReadSetting(QStringLiteral("screenshot_resolution_factor"), 0).toUInt());
    UISettings::values.select_user_on_boot =
        ReadSetting(QStringLiteral("select_user_on_boot"), false).toBool();

    ReadUIGamelistValues();
    ReadUILayoutValues();
    ReadPathValues();
    ReadShortcutValues();

    UISettings::values.single_window_mode =
        ReadSetting(QStringLiteral("singleWindowMode"), true).toBool();
    UISettings::values.fullscreen = ReadSetting(QStringLiteral("fullscreen"), false).toBool();
    UISettings::values.display_titlebar =
        ReadSetting(QStringLiteral("displayTitleBars"), true).toBool();
    UISettings::values.show_filter_bar =
        ReadSetting(QStringLiteral("showFilterBar"), true).toBool();
    UISettings::values.show_status_bar =
        ReadSetting(QStringLiteral("showStatusBar"), true).toBool();
    UISettings::values.confirm_before_closing =
        ReadSetting(QStringLiteral("confirmClose"), true).toBool();
    UISettings::values.first_start = ReadSetting(QStringLiteral("firstStart"), true).toBool();
    UISettings::values.callout_flags = ReadSetting(QStringLiteral("calloutFlags"), 0).toUInt();
    UISettings::values.show_console = ReadSetting(QStringLiteral("showConsole"), false).toBool();
    UISettings::values.profile_index = ReadSetting(QStringLiteral("profileIndex"), 0).toUInt();

    ApplyDefaultProfileIfInputInvalid();

    qt_config->endGroup();
}

void Config::ReadUIGamelistValues() {
    qt_config->beginGroup(QStringLiteral("UIGameList"));

    UISettings::values.show_unknown = ReadSetting(QStringLiteral("show_unknown"), true).toBool();
    UISettings::values.show_add_ons = ReadSetting(QStringLiteral("show_add_ons"), true).toBool();
    UISettings::values.icon_size = ReadSetting(QStringLiteral("icon_size"), 64).toUInt();
    UISettings::values.row_1_text_id = ReadSetting(QStringLiteral("row_1_text_id"), 3).toUInt();
    UISettings::values.row_2_text_id = ReadSetting(QStringLiteral("row_2_text_id"), 2).toUInt();
    UISettings::values.cache_game_list =
        ReadSetting(QStringLiteral("cache_game_list"), true).toBool();

    qt_config->endGroup();
}

void Config::ReadUILayoutValues() {
    qt_config->beginGroup(QStringLiteral("UILayout"));

    UISettings::values.geometry = ReadSetting(QStringLiteral("geometry")).toByteArray();
    UISettings::values.state = ReadSetting(QStringLiteral("state")).toByteArray();
    UISettings::values.renderwindow_geometry =
        ReadSetting(QStringLiteral("geometryRenderWindow")).toByteArray();
    UISettings::values.gamelist_header_state =
        ReadSetting(QStringLiteral("gameListHeaderState")).toByteArray();
    UISettings::values.microprofile_geometry =
        ReadSetting(QStringLiteral("microProfileDialogGeometry")).toByteArray();
    UISettings::values.microprofile_visible =
        ReadSetting(QStringLiteral("microProfileDialogVisible"), false).toBool();

    qt_config->endGroup();
}

void Config::ReadWebServiceValues() {
    qt_config->beginGroup(QStringLiteral("WebService"));

    Settings::values.enable_telemetry =
        ReadSetting(QStringLiteral("enable_telemetry"), true).toBool();
    Settings::values.web_api_url =
        ReadSetting(QStringLiteral("web_api_url"), QStringLiteral("https://api.yuzu-emu.org"))
            .toString()
            .toStdString();
    Settings::values.yuzu_username =
        ReadSetting(QStringLiteral("yuzu_username")).toString().toStdString();
    Settings::values.yuzu_token =
        ReadSetting(QStringLiteral("yuzu_token")).toString().toStdString();

    qt_config->endGroup();
}

void Config::ReadValues() {
    ReadControlValues();
    ReadCoreValues();
    ReadRendererValues();
    ReadAudioValues();
    ReadDataStorageValues();
    ReadSystemValues();
    ReadMiscellaneousValues();
    ReadDebuggingValues();
    ReadWebServiceValues();
    ReadDisabledAddOnValues();
    ReadUIValues();
}

void Config::SavePlayerValues() {
    for (std::size_t p = 0; p < Settings::values.players.size(); ++p) {
        const auto& player = Settings::values.players[p];

        WriteSetting(QStringLiteral("player_%1_connected").arg(p), player.connected, false);
        WriteSetting(QStringLiteral("player_%1_type").arg(p), static_cast<u8>(player.type),
                     static_cast<u8>(Settings::ControllerType::DualJoycon));

        WriteSetting(QStringLiteral("player_%1_body_color_left").arg(p), player.body_color_left,
                     Settings::JOYCON_BODY_NEON_BLUE);
        WriteSetting(QStringLiteral("player_%1_body_color_right").arg(p), player.body_color_right,
                     Settings::JOYCON_BODY_NEON_RED);
        WriteSetting(QStringLiteral("player_%1_button_color_left").arg(p), player.button_color_left,
                     Settings::JOYCON_BUTTONS_NEON_BLUE);
        WriteSetting(QStringLiteral("player_%1_button_color_right").arg(p),
                     player.button_color_right, Settings::JOYCON_BUTTONS_NEON_RED);

        for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
            const std::string default_param =
                InputCommon::GenerateKeyboardParam(default_buttons[i]);
            WriteSetting(QStringLiteral("player_%1_").arg(p) +
                             QString::fromStdString(Settings::NativeButton::mapping[i]),
                         QString::fromStdString(player.buttons[i]),
                         QString::fromStdString(default_param));
        }
        for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
            const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
                default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
                default_analogs[i][3], default_analogs[i][4], 0.5f);
            WriteSetting(QStringLiteral("player_%1_").arg(p) +
                             QString::fromStdString(Settings::NativeAnalog::mapping[i]),
                         QString::fromStdString(player.analogs[i]),
                         QString::fromStdString(default_param));
        }
    }
}

void Config::SaveDebugValues() {
    WriteSetting(QStringLiteral("debug_pad_enabled"), Settings::values.debug_pad_enabled, false);
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        const std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
        WriteSetting(QStringLiteral("debug_pad_") +
                         QString::fromStdString(Settings::NativeButton::mapping[i]),
                     QString::fromStdString(Settings::values.debug_pad_buttons[i]),
                     QString::fromStdString(default_param));
    }
    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
            default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
            default_analogs[i][3], default_analogs[i][4], 0.5f);
        WriteSetting(QStringLiteral("debug_pad_") +
                         QString::fromStdString(Settings::NativeAnalog::mapping[i]),
                     QString::fromStdString(Settings::values.debug_pad_analogs[i]),
                     QString::fromStdString(default_param));
    }
}

void Config::SaveMouseValues() {
    WriteSetting(QStringLiteral("mouse_enabled"), Settings::values.mouse_enabled, false);

    for (int i = 0; i < Settings::NativeMouseButton::NumMouseButtons; ++i) {
        const std::string default_param =
            InputCommon::GenerateKeyboardParam(default_mouse_buttons[i]);
        WriteSetting(QStringLiteral("mouse_") +
                         QString::fromStdString(Settings::NativeMouseButton::mapping[i]),
                     QString::fromStdString(Settings::values.mouse_buttons[i]),
                     QString::fromStdString(default_param));
    }
}

void Config::SaveTouchscreenValues() {
    const auto& touchscreen = Settings::values.touchscreen;

    WriteSetting(QStringLiteral("touchscreen_enabled"), touchscreen.enabled, true);
    WriteSetting(QStringLiteral("touchscreen_device"), QString::fromStdString(touchscreen.device),
                 QStringLiteral("engine:emu_window"));

    WriteSetting(QStringLiteral("touchscreen_finger"), touchscreen.finger, 0);
    WriteSetting(QStringLiteral("touchscreen_angle"), touchscreen.rotation_angle, 0);
    WriteSetting(QStringLiteral("touchscreen_diameter_x"), touchscreen.diameter_x, 15);
    WriteSetting(QStringLiteral("touchscreen_diameter_y"), touchscreen.diameter_y, 15);
}

void Config::SaveValues() {
    SaveControlValues();
    SaveCoreValues();
    SaveRendererValues();
    SaveAudioValues();
    SaveDataStorageValues();
    SaveSystemValues();
    SaveMiscellaneousValues();
    SaveDebuggingValues();
    SaveWebServiceValues();
    SaveDisabledAddOnValues();
    SaveUIValues();
}

void Config::SaveAudioValues() {
    qt_config->beginGroup(QStringLiteral("Audio"));

    WriteSetting(QStringLiteral("output_engine"), QString::fromStdString(Settings::values.sink_id),
                 QStringLiteral("auto"));
    WriteSetting(QStringLiteral("enable_audio_stretching"),
                 Settings::values.enable_audio_stretching, true);
    WriteSetting(QStringLiteral("output_device"),
                 QString::fromStdString(Settings::values.audio_device_id), QStringLiteral("auto"));
    WriteSetting(QStringLiteral("volume"), Settings::values.volume, 1.0f);

    qt_config->endGroup();
}

void Config::SaveControlValues() {
    qt_config->beginGroup(QStringLiteral("Controls"));

    SavePlayerValues();
    SaveDebugValues();
    SaveMouseValues();
    SaveTouchscreenValues();

    WriteSetting(QStringLiteral("motion_device"),
                 QString::fromStdString(Settings::values.motion_device),
                 QStringLiteral("engine:motion_emu,update_period:100,sensitivity:0.01"));
    WriteSetting(QStringLiteral("keyboard_enabled"), Settings::values.keyboard_enabled, false);

    qt_config->endGroup();
}

void Config::SaveCoreValues() {
    qt_config->beginGroup(QStringLiteral("Core"));

    WriteSetting(QStringLiteral("use_multi_core"), Settings::values.use_multi_core, false);

    qt_config->endGroup();
}

void Config::SaveDataStorageValues() {
    qt_config->beginGroup(QStringLiteral("Data Storage"));

    WriteSetting(QStringLiteral("use_virtual_sd"), Settings::values.use_virtual_sd, true);
    WriteSetting(QStringLiteral("nand_directory"),
                 QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::NANDDir)),
                 QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::NANDDir)));
    WriteSetting(QStringLiteral("sdmc_directory"),
                 QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir)),
                 QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir)));
    WriteSetting(QStringLiteral("load_directory"),
                 QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::LoadDir)),
                 QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::LoadDir)));
    WriteSetting(QStringLiteral("dump_directory"),
                 QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::DumpDir)),
                 QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::DumpDir)));
    WriteSetting(QStringLiteral("cache_directory"),
                 QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::CacheDir)),
                 QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::CacheDir)));
    WriteSetting(QStringLiteral("gamecard_inserted"), Settings::values.gamecard_inserted, false);
    WriteSetting(QStringLiteral("gamecard_current_game"), Settings::values.gamecard_current_game,
                 false);
    WriteSetting(QStringLiteral("gamecard_path"),
                 QString::fromStdString(Settings::values.gamecard_path), QStringLiteral(""));
    WriteSetting(QStringLiteral("nand_total_size"),
                 QVariant::fromValue<u64>(static_cast<u64>(Settings::values.nand_total_size)),
                 QVariant::fromValue<u64>(static_cast<u64>(Settings::NANDTotalSize::S29_1GB)));
    WriteSetting(QStringLiteral("nand_user_size"),
                 QVariant::fromValue<u64>(static_cast<u64>(Settings::values.nand_user_size)),
                 QVariant::fromValue<u64>(static_cast<u64>(Settings::NANDUserSize::S26GB)));
    WriteSetting(QStringLiteral("nand_system_size"),
                 QVariant::fromValue<u64>(static_cast<u64>(Settings::values.nand_system_size)),
                 QVariant::fromValue<u64>(static_cast<u64>(Settings::NANDSystemSize::S2_5GB)));
    WriteSetting(QStringLiteral("sdmc_size"),
                 QVariant::fromValue<u64>(static_cast<u64>(Settings::values.sdmc_size)),
                 QVariant::fromValue<u64>(static_cast<u64>(Settings::SDMCSize::S16GB)));
    qt_config->endGroup();
}

void Config::SaveDebuggingValues() {
    qt_config->beginGroup(QStringLiteral("Debugging"));

    // Intentionally not using the QT default setting as this is intended to be changed in the ini
    qt_config->setValue(QStringLiteral("record_frame_times"), Settings::values.record_frame_times);
    WriteSetting(QStringLiteral("use_gdbstub"), Settings::values.use_gdbstub, false);
    WriteSetting(QStringLiteral("gdbstub_port"), Settings::values.gdbstub_port, 24689);
    WriteSetting(QStringLiteral("program_args"),
                 QString::fromStdString(Settings::values.program_args), QStringLiteral(""));
    WriteSetting(QStringLiteral("dump_exefs"), Settings::values.dump_exefs, false);
    WriteSetting(QStringLiteral("dump_nso"), Settings::values.dump_nso, false);
    WriteSetting(QStringLiteral("quest_flag"), Settings::values.quest_flag, false);

    qt_config->endGroup();
}

void Config::SaveDisabledAddOnValues() {
    qt_config->beginWriteArray(QStringLiteral("DisabledAddOns"));

    int i = 0;
    for (const auto& elem : Settings::values.disabled_addons) {
        qt_config->setArrayIndex(i);
        WriteSetting(QStringLiteral("title_id"), QVariant::fromValue<u64>(elem.first), 0);
        qt_config->beginWriteArray(QStringLiteral("disabled"));
        for (std::size_t j = 0; j < elem.second.size(); ++j) {
            qt_config->setArrayIndex(static_cast<int>(j));
            WriteSetting(QStringLiteral("d"), QString::fromStdString(elem.second[j]),
                         QStringLiteral(""));
        }
        qt_config->endArray();
        ++i;
    }

    qt_config->endArray();
}

void Config::SaveMiscellaneousValues() {
    qt_config->beginGroup(QStringLiteral("Miscellaneous"));

    WriteSetting(QStringLiteral("log_filter"), QString::fromStdString(Settings::values.log_filter),
                 QStringLiteral("*:Info"));
    WriteSetting(QStringLiteral("use_dev_keys"), Settings::values.use_dev_keys, false);

    qt_config->endGroup();
}

void Config::SavePathValues() {
    qt_config->beginGroup(QStringLiteral("Paths"));

    WriteSetting(QStringLiteral("romsPath"), UISettings::values.roms_path);
    WriteSetting(QStringLiteral("symbolsPath"), UISettings::values.symbols_path);
    WriteSetting(QStringLiteral("screenshotPath"), UISettings::values.screenshot_path);
    qt_config->beginWriteArray(QStringLiteral("gamedirs"));
    for (int i = 0; i < UISettings::values.game_dirs.size(); ++i) {
        qt_config->setArrayIndex(i);
        const auto& game_dir = UISettings::values.game_dirs[i];
        WriteSetting(QStringLiteral("path"), game_dir.path);
        WriteSetting(QStringLiteral("deep_scan"), game_dir.deep_scan, false);
        WriteSetting(QStringLiteral("expanded"), game_dir.expanded, true);
    }
    qt_config->endArray();
    WriteSetting(QStringLiteral("recentFiles"), UISettings::values.recent_files);

    qt_config->endGroup();
}

void Config::SaveRendererValues() {
    qt_config->beginGroup(QStringLiteral("Renderer"));

    WriteSetting(QStringLiteral("resolution_factor"),
                 static_cast<double>(Settings::values.resolution_factor), 1.0);
    WriteSetting(QStringLiteral("use_frame_limit"), Settings::values.use_frame_limit, true);
    WriteSetting(QStringLiteral("frame_limit"), Settings::values.frame_limit, 100);
    WriteSetting(QStringLiteral("use_disk_shader_cache"), Settings::values.use_disk_shader_cache,
                 true);
    WriteSetting(QStringLiteral("use_accurate_gpu_emulation"),
                 Settings::values.use_accurate_gpu_emulation, false);
    WriteSetting(QStringLiteral("use_asynchronous_gpu_emulation"),
                 Settings::values.use_asynchronous_gpu_emulation, false);
    WriteSetting(QStringLiteral("use_resolution_scanner"), Settings::values.use_resolution_scanner,
                 false);
    WriteSetting(QStringLiteral("force_30fps_mode"), Settings::values.force_30fps_mode, false);

    // Cast to double because Qt's written float values are not human-readable
    WriteSetting(QStringLiteral("bg_red"), static_cast<double>(Settings::values.bg_red), 0.0);
    WriteSetting(QStringLiteral("bg_green"), static_cast<double>(Settings::values.bg_green), 0.0);
    WriteSetting(QStringLiteral("bg_blue"), static_cast<double>(Settings::values.bg_blue), 0.0);

    qt_config->endGroup();
}

void Config::SaveShortcutValues() {
    qt_config->beginGroup(QStringLiteral("Shortcuts"));

    // Lengths of UISettings::values.shortcuts & default_hotkeys are same.
    // However, their ordering must also be the same.
    for (std::size_t i = 0; i < default_hotkeys.size(); i++) {
        const auto& [name, group, shortcut] = UISettings::values.shortcuts[i];
        const auto& default_hotkey = default_hotkeys[i].shortcut;

        qt_config->beginGroup(group);
        qt_config->beginGroup(name);
        WriteSetting(QStringLiteral("KeySeq"), shortcut.first, default_hotkey.first);
        WriteSetting(QStringLiteral("Context"), shortcut.second, default_hotkey.second);
        qt_config->endGroup();
        qt_config->endGroup();
    }

    qt_config->endGroup();
}

void Config::SaveSystemValues() {
    qt_config->beginGroup(QStringLiteral("System"));

    WriteSetting(QStringLiteral("use_docked_mode"), Settings::values.use_docked_mode, false);
    WriteSetting(QStringLiteral("current_user"), Settings::values.current_user, 0);
    WriteSetting(QStringLiteral("language_index"), Settings::values.language_index, 1);

    WriteSetting(QStringLiteral("rng_seed_enabled"), Settings::values.rng_seed.has_value(), false);
    WriteSetting(QStringLiteral("rng_seed"), Settings::values.rng_seed.value_or(0), 0);

    WriteSetting(QStringLiteral("custom_rtc_enabled"), Settings::values.custom_rtc.has_value(),
                 false);
    WriteSetting(QStringLiteral("custom_rtc"),
                 QVariant::fromValue<long long>(
                     Settings::values.custom_rtc.value_or(std::chrono::seconds{}).count()),
                 0);

    qt_config->endGroup();
}

void Config::SaveUIValues() {
    qt_config->beginGroup(QStringLiteral("UI"));

    WriteSetting(QStringLiteral("theme"), UISettings::values.theme,
                 QString::fromUtf8(UISettings::themes[0].second));
    WriteSetting(QStringLiteral("enable_discord_presence"),
                 UISettings::values.enable_discord_presence, true);
    WriteSetting(QStringLiteral("screenshot_resolution_factor"),
                 UISettings::values.screenshot_resolution_factor, 0);
    WriteSetting(QStringLiteral("select_user_on_boot"), UISettings::values.select_user_on_boot,
                 false);

    SaveUIGamelistValues();
    SaveUILayoutValues();
    SavePathValues();
    SaveShortcutValues();

    WriteSetting(QStringLiteral("singleWindowMode"), UISettings::values.single_window_mode, true);
    WriteSetting(QStringLiteral("fullscreen"), UISettings::values.fullscreen, false);
    WriteSetting(QStringLiteral("displayTitleBars"), UISettings::values.display_titlebar, true);
    WriteSetting(QStringLiteral("showFilterBar"), UISettings::values.show_filter_bar, true);
    WriteSetting(QStringLiteral("showStatusBar"), UISettings::values.show_status_bar, true);
    WriteSetting(QStringLiteral("confirmClose"), UISettings::values.confirm_before_closing, true);
    WriteSetting(QStringLiteral("firstStart"), UISettings::values.first_start, true);
    WriteSetting(QStringLiteral("calloutFlags"), UISettings::values.callout_flags, 0);
    WriteSetting(QStringLiteral("showConsole"), UISettings::values.show_console, false);
    WriteSetting(QStringLiteral("profileIndex"), UISettings::values.profile_index, 0);

    qt_config->endGroup();
}

void Config::SaveUIGamelistValues() {
    qt_config->beginGroup(QStringLiteral("UIGameList"));

    WriteSetting(QStringLiteral("show_unknown"), UISettings::values.show_unknown, true);
    WriteSetting(QStringLiteral("show_add_ons"), UISettings::values.show_add_ons, true);
    WriteSetting(QStringLiteral("icon_size"), UISettings::values.icon_size, 64);
    WriteSetting(QStringLiteral("row_1_text_id"), UISettings::values.row_1_text_id, 3);
    WriteSetting(QStringLiteral("row_2_text_id"), UISettings::values.row_2_text_id, 2);
    WriteSetting(QStringLiteral("cache_game_list"), UISettings::values.cache_game_list, true);

    qt_config->endGroup();
}

void Config::SaveUILayoutValues() {
    qt_config->beginGroup(QStringLiteral("UILayout"));

    WriteSetting(QStringLiteral("geometry"), UISettings::values.geometry);
    WriteSetting(QStringLiteral("state"), UISettings::values.state);
    WriteSetting(QStringLiteral("geometryRenderWindow"), UISettings::values.renderwindow_geometry);
    WriteSetting(QStringLiteral("gameListHeaderState"), UISettings::values.gamelist_header_state);
    WriteSetting(QStringLiteral("microProfileDialogGeometry"),
                 UISettings::values.microprofile_geometry);
    WriteSetting(QStringLiteral("microProfileDialogVisible"),
                 UISettings::values.microprofile_visible, false);

    qt_config->endGroup();
}

void Config::SaveWebServiceValues() {
    qt_config->beginGroup(QStringLiteral("WebService"));

    WriteSetting(QStringLiteral("enable_telemetry"), Settings::values.enable_telemetry, true);
    WriteSetting(QStringLiteral("web_api_url"),
                 QString::fromStdString(Settings::values.web_api_url),
                 QStringLiteral("https://api.yuzu-emu.org"));
    WriteSetting(QStringLiteral("yuzu_username"),
                 QString::fromStdString(Settings::values.yuzu_username));
    WriteSetting(QStringLiteral("yuzu_token"), QString::fromStdString(Settings::values.yuzu_token));

    qt_config->endGroup();
}

QVariant Config::ReadSetting(const QString& name) const {
    return qt_config->value(name);
}

QVariant Config::ReadSetting(const QString& name, const QVariant& default_value) const {
    QVariant result;
    if (qt_config->value(name + QStringLiteral("/default"), false).toBool()) {
        result = default_value;
    } else {
        result = qt_config->value(name, default_value);
    }
    return result;
}

void Config::WriteSetting(const QString& name, const QVariant& value) {
    qt_config->setValue(name, value);
}

void Config::WriteSetting(const QString& name, const QVariant& value,
                          const QVariant& default_value) {
    qt_config->setValue(name + QStringLiteral("/default"), value == default_value);
    qt_config->setValue(name, value);
}

void Config::Reload() {
    ReadValues();
    // To apply default value changes
    SaveValues();
    Settings::Apply();
}

void Config::Save() {
    SaveValues();
}
