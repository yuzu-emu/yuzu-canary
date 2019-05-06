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
// QKeySequnce(...).toString() is NOT ALLOWED HERE.
// This must be in alphabetical order according to action name as it must have the same order as
// UISetting::values.shortcuts, which is alphabetically ordered.
const std::array<UISettings::Shortcut, 15> Config::default_hotkeys{
    {{"Capture Screenshot", "Main Window", {"Ctrl+P", Qt::ApplicationShortcut}},
     {"Continue/Pause Emulation", "Main Window", {"F4", Qt::WindowShortcut}},
     {"Decrease Speed Limit", "Main Window", {"-", Qt::ApplicationShortcut}},
     {"Exit yuzu", "Main Window", {"Ctrl+Q", Qt::WindowShortcut}},
     {"Exit Fullscreen", "Main Window", {"Esc", Qt::WindowShortcut}},
     {"Fullscreen", "Main Window", {"F11", Qt::WindowShortcut}},
     {"Increase Speed Limit", "Main Window", {"+", Qt::ApplicationShortcut}},
     {"Load Amiibo", "Main Window", {"F2", Qt::ApplicationShortcut}},
     {"Load File", "Main Window", {"Ctrl+O", Qt::WindowShortcut}},
     {"Restart Emulation", "Main Window", {"F6", Qt::WindowShortcut}},
     {"Stop Emulation", "Main Window", {"F5", Qt::WindowShortcut}},
     {"Toggle Filter Bar", "Main Window", {"Ctrl+F", Qt::WindowShortcut}},
     {"Toggle Speed Limit", "Main Window", {"Ctrl+Z", Qt::ApplicationShortcut}},
     {"Toggle Status Bar", "Main Window", {"Ctrl+S", Qt::WindowShortcut}},
     {"Change Docked Mode", "Main Window", {"F10", Qt::ApplicationShortcut}}}};

void Config::ReadPlayerValues() {
    for (std::size_t p = 0; p < Settings::values.players.size(); ++p) {
        auto& player = Settings::values.players[p];

        player.connected = ReadSetting(QString("player_%1_connected").arg(p), false).toBool();

        player.type = static_cast<Settings::ControllerType>(
            qt_config
                ->value(QString("player_%1_type").arg(p),
                        static_cast<u8>(Settings::ControllerType::DualJoycon))
                .toUInt());

        player.body_color_left = qt_config
                                     ->value(QString("player_%1_body_color_left").arg(p),
                                             Settings::JOYCON_BODY_NEON_BLUE)
                                     .toUInt();
        player.body_color_right = qt_config
                                      ->value(QString("player_%1_body_color_right").arg(p),
                                              Settings::JOYCON_BODY_NEON_RED)
                                      .toUInt();
        player.button_color_left = qt_config
                                       ->value(QString("player_%1_button_color_left").arg(p),
                                               Settings::JOYCON_BUTTONS_NEON_BLUE)
                                       .toUInt();
        player.button_color_right = qt_config
                                        ->value(QString("player_%1_button_color_right").arg(p),
                                                Settings::JOYCON_BUTTONS_NEON_RED)
                                        .toUInt();

        for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
            std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
            player.buttons[i] =
                qt_config
                    ->value(QString("player_%1_").arg(p) + Settings::NativeButton::mapping[i],
                            QString::fromStdString(default_param))
                    .toString()
                    .toStdString();
            if (player.buttons[i].empty())
                player.buttons[i] = default_param;
        }

        for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
            std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
                default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
                default_analogs[i][3], default_analogs[i][4], 0.5f);
            player.analogs[i] =
                qt_config
                    ->value(QString("player_%1_").arg(p) + Settings::NativeAnalog::mapping[i],
                            QString::fromStdString(default_param))
                    .toString()
                    .toStdString();
            if (player.analogs[i].empty())
                player.analogs[i] = default_param;
        }
    }

    std::stable_partition(
        Settings::values.players.begin(),
        Settings::values.players.begin() +
            Service::HID::Controller_NPad::NPadIdToIndex(Service::HID::NPAD_HANDHELD),
        [](const auto& player) { return player.connected; });
}

void Config::ReadDebugValues() {
    Settings::values.debug_pad_enabled = ReadSetting("debug_pad_enabled", false).toBool();
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
    Settings::values.keyboard_enabled = ReadSetting("keyboard_enabled", false).toBool();

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
    Settings::values.mouse_enabled = ReadSetting("mouse_enabled", false).toBool();

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
    Settings::values.touchscreen.enabled = ReadSetting("touchscreen_enabled", true).toBool();
    Settings::values.touchscreen.device =
        ReadSetting("touchscreen_device", "engine:emu_window").toString().toStdString();

    Settings::values.touchscreen.finger = ReadSetting("touchscreen_finger", 0).toUInt();
    Settings::values.touchscreen.rotation_angle = ReadSetting("touchscreen_angle", 0).toUInt();
    Settings::values.touchscreen.diameter_x = ReadSetting("touchscreen_diameter_x", 15).toUInt();
    Settings::values.touchscreen.diameter_y = ReadSetting("touchscreen_diameter_y", 15).toUInt();
    qt_config->endGroup();
}

void Config::ApplyDefaultProfileIfInputInvalid() {
    if (!std::any_of(Settings::values.players.begin(), Settings::values.players.end(),
                     [](const Settings::PlayerInput& in) { return in.connected; })) {
        ApplyInputProfileConfiguration(UISettings::values.profile_index);
    }
}

void Config::ReadValues() {
    qt_config->beginGroup("Controls");

    ReadPlayerValues();
    ReadDebugValues();
    ReadKeyboardValues();
    ReadMouseValues();
    ReadTouchscreenValues();

    Settings::values.motion_device =
        ReadSetting("motion_device", "engine:motion_emu,update_period:100,sensitivity:0.01")
            .toString()
            .toStdString();

    qt_config->beginGroup("Core");
    Settings::values.use_cpu_jit = ReadSetting("use_cpu_jit", true).toBool();
    Settings::values.use_multi_core = ReadSetting("use_multi_core", false).toBool();
    qt_config->endGroup();

    qt_config->beginGroup("Renderer");
    Settings::values.resolution_factor = ReadSetting("resolution_factor", 1.0).toFloat();
    Settings::values.use_frame_limit = ReadSetting("use_frame_limit", true).toBool();
    Settings::values.frame_limit = ReadSetting("frame_limit", 100).toInt();
    Settings::values.use_compatibility_profile =
        ReadSetting("use_compatibility_profile", true).toBool();
    Settings::values.use_disk_shader_cache = ReadSetting("use_disk_shader_cache", true).toBool();
    Settings::values.use_accurate_gpu_emulation =
        ReadSetting("use_accurate_gpu_emulation", false).toBool();
    Settings::values.use_asynchronous_gpu_emulation =
        ReadSetting("use_asynchronous_gpu_emulation", false).toBool();
    Settings::values.force_30fps_mode = ReadSetting("force_30fps_mode", false).toBool();
    Settings::values.fps_mode = ReadSetting("fps_mode", 60).toInt();

    Settings::values.bg_red = ReadSetting("bg_red", 0.0).toFloat();
    Settings::values.bg_green = ReadSetting("bg_green", 0.0).toFloat();
    Settings::values.bg_blue = ReadSetting("bg_blue", 0.0).toFloat();
    qt_config->endGroup();

    qt_config->beginGroup("Audio");
    Settings::values.sink_id = ReadSetting("output_engine", "auto").toString().toStdString();
    Settings::values.enable_audio_stretching =
        ReadSetting("enable_audio_stretching", true).toBool();
    Settings::values.audio_device_id =
        ReadSetting("output_device", "auto").toString().toStdString();
    Settings::values.volume = ReadSetting("volume", 1).toFloat();
    qt_config->endGroup();

    qt_config->beginGroup("Data Storage");
    Settings::values.use_virtual_sd = ReadSetting("use_virtual_sd", true).toBool();
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
    Settings::values.use_cpu_jit = ReadSetting("use_cpu_jit", true).toBool();
    Settings::values.use_multi_core = ReadSetting("use_multi_core", false).toBool();
    qt_config->endGroup();

    qt_config->beginGroup("System");
    Settings::values.use_docked_mode = ReadSetting("use_docked_mode", false).toBool();

    Settings::values.current_user =
        std::clamp<int>(ReadSetting("current_user", 0).toInt(), 0, Service::Account::MAX_USERS - 1);

    Settings::values.language_index = ReadSetting("language_index", 1).toInt();

    const auto rng_seed_enabled = ReadSetting("rng_seed_enabled", false).toBool();
    if (rng_seed_enabled) {
        Settings::values.rng_seed = ReadSetting("rng_seed", 0).toULongLong();
    } else {
        Settings::values.rng_seed = std::nullopt;
    }

    const auto custom_rtc_enabled = ReadSetting("custom_rtc_enabled", false).toBool();
    if (custom_rtc_enabled) {
        Settings::values.custom_rtc =
            std::chrono::seconds(ReadSetting("custom_rtc", 0).toULongLong());
    } else {
        Settings::values.custom_rtc = std::nullopt;
    }

    qt_config->endGroup();

    qt_config->beginGroup("Miscellaneous");
    Settings::values.log_filter = ReadSetting("log_filter", "*:Info").toString().toStdString();
    Settings::values.use_dev_keys = ReadSetting("use_dev_keys", false).toBool();
    qt_config->endGroup();

    qt_config->beginGroup("Debugging");
    Settings::values.use_gdbstub = ReadSetting("use_gdbstub", false).toBool();
    Settings::values.gdbstub_port = ReadSetting("gdbstub_port", 24689).toInt();
    Settings::values.program_args = ReadSetting("program_args", "").toString().toStdString();
    Settings::values.dump_exefs = ReadSetting("dump_exefs", false).toBool();
    Settings::values.dump_nso = ReadSetting("dump_nso", false).toBool();
    qt_config->endGroup();

    qt_config->beginGroup("WebService");
    Settings::values.enable_telemetry = ReadSetting("enable_telemetry", true).toBool();
    Settings::values.web_api_url =
        ReadSetting("web_api_url", "https://api.yuzu-emu.org").toString().toStdString();
    Settings::values.yuzu_username = ReadSetting("yuzu_username").toString().toStdString();
    Settings::values.yuzu_token = ReadSetting("yuzu_token").toString().toStdString();
    qt_config->endGroup();

    const auto size = qt_config->beginReadArray("DisabledAddOns");
    for (int i = 0; i < size; ++i) {
        qt_config->setArrayIndex(i);
        const auto title_id = ReadSetting("title_id", 0).toULongLong();
        std::vector<std::string> out;
        const auto d_size = qt_config->beginReadArray("disabled");
        for (int j = 0; j < d_size; ++j) {
            qt_config->setArrayIndex(j);
            out.push_back(ReadSetting("d", "").toString().toStdString());
        }
        qt_config->endArray();
        Settings::values.disabled_addons.insert_or_assign(title_id, out);
    }
    qt_config->endArray();

    qt_config->beginGroup("UI");
    UISettings::values.theme = ReadSetting("theme", UISettings::themes[0].second).toString();
    UISettings::values.enable_discord_presence =
        ReadSetting("enable_discord_presence", true).toBool();
    UISettings::values.screenshot_resolution_factor =
        static_cast<u16>(ReadSetting("screenshot_resolution_factor", 0).toUInt());
    UISettings::values.select_user_on_boot = ReadSetting("select_user_on_boot", false).toBool();

    qt_config->beginGroup("UIGameList");
    UISettings::values.show_unknown = ReadSetting("show_unknown", true).toBool();
    UISettings::values.show_add_ons = ReadSetting("show_add_ons", true).toBool();
    UISettings::values.icon_size = ReadSetting("icon_size", 64).toUInt();
    UISettings::values.row_1_text_id = ReadSetting("row_1_text_id", 3).toUInt();
    UISettings::values.row_2_text_id = ReadSetting("row_2_text_id", 2).toUInt();
    qt_config->endGroup();

    qt_config->beginGroup("UILayout");
    UISettings::values.geometry = ReadSetting("geometry").toByteArray();
    UISettings::values.state = ReadSetting("state").toByteArray();
    UISettings::values.renderwindow_geometry = ReadSetting("geometryRenderWindow").toByteArray();
    UISettings::values.gamelist_header_state = ReadSetting("gameListHeaderState").toByteArray();
    UISettings::values.microprofile_geometry =
        ReadSetting("microProfileDialogGeometry").toByteArray();
    UISettings::values.microprofile_visible =
        ReadSetting("microProfileDialogVisible", false).toBool();
    qt_config->endGroup();

    qt_config->beginGroup("Paths");
    UISettings::values.roms_path = ReadSetting("romsPath").toString();
    UISettings::values.symbols_path = ReadSetting("symbolsPath").toString();
    UISettings::values.game_directory_path = ReadSetting("gameListRootDir", ".").toString();
    UISettings::values.game_directory_deepscan = ReadSetting("gameListDeepScan", false).toBool();
    UISettings::values.recent_files = ReadSetting("recentFiles").toStringList();
    qt_config->endGroup();

    qt_config->beginGroup("Shortcuts");
    for (auto [name, group, shortcut] : default_hotkeys) {
        auto [keyseq, context] = shortcut;
        qt_config->beginGroup(group);
        qt_config->beginGroup(name);
        UISettings::values.shortcuts.push_back(
            {name,
             group,
             {ReadSetting("KeySeq", keyseq).toString(), ReadSetting("Context", context).toInt()}});
        qt_config->endGroup();
        qt_config->endGroup();
    }
    qt_config->endGroup();

    UISettings::values.single_window_mode = ReadSetting("singleWindowMode", true).toBool();
    UISettings::values.fullscreen = ReadSetting("fullscreen", false).toBool();
    UISettings::values.display_titlebar = ReadSetting("displayTitleBars", true).toBool();
    UISettings::values.show_filter_bar = ReadSetting("showFilterBar", true).toBool();
    UISettings::values.show_status_bar = ReadSetting("showStatusBar", true).toBool();
    UISettings::values.confirm_before_closing = ReadSetting("confirmClose", true).toBool();
    UISettings::values.first_start = ReadSetting("firstStart", true).toBool();
    UISettings::values.callout_flags = ReadSetting("calloutFlags", 0).toUInt();
    UISettings::values.show_console = ReadSetting("showConsole", false).toBool();
    UISettings::values.profile_index = ReadSetting("profileIndex", 0).toUInt();

    ApplyDefaultProfileIfInputInvalid();

    qt_config->endGroup();
}

void Config::SavePlayerValues() {
    for (std::size_t p = 0; p < Settings::values.players.size(); ++p) {
        const auto& player = Settings::values.players[p];

        WriteSetting(QString("player_%1_connected").arg(p), player.connected, false);
        WriteSetting(QString("player_%1_type").arg(p), static_cast<u8>(player.type),
                     static_cast<u8>(Settings::ControllerType::DualJoycon));

        WriteSetting(QString("player_%1_body_color_left").arg(p), player.body_color_left,
                     Settings::JOYCON_BODY_NEON_BLUE);
        WriteSetting(QString("player_%1_body_color_right").arg(p), player.body_color_right,
                     Settings::JOYCON_BODY_NEON_RED);
        WriteSetting(QString("player_%1_button_color_left").arg(p), player.button_color_left,
                     Settings::JOYCON_BUTTONS_NEON_BLUE);
        WriteSetting(QString("player_%1_button_color_right").arg(p), player.button_color_right,
                     Settings::JOYCON_BUTTONS_NEON_RED);

        for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
            std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
            WriteSetting(QString("player_%1_").arg(p) +
                             QString::fromStdString(Settings::NativeButton::mapping[i]),
                         QString::fromStdString(player.buttons[i]),
                         QString::fromStdString(default_param));
        }
        for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
            std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
                default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
                default_analogs[i][3], default_analogs[i][4], 0.5f);
            WriteSetting(QString("player_%1_").arg(p) +
                             QString::fromStdString(Settings::NativeAnalog::mapping[i]),
                         QString::fromStdString(player.analogs[i]),
                         QString::fromStdString(default_param));
        }
    }
}

void Config::SaveDebugValues() {
    WriteSetting("debug_pad_enabled", Settings::values.debug_pad_enabled, false);
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
        WriteSetting(QString("debug_pad_") +
                         QString::fromStdString(Settings::NativeButton::mapping[i]),
                     QString::fromStdString(Settings::values.debug_pad_buttons[i]),
                     QString::fromStdString(default_param));
    }
    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
            default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
            default_analogs[i][3], default_analogs[i][4], 0.5f);
        WriteSetting(QString("debug_pad_") +
                         QString::fromStdString(Settings::NativeAnalog::mapping[i]),
                     QString::fromStdString(Settings::values.debug_pad_analogs[i]),
                     QString::fromStdString(default_param));
    }
}

void Config::SaveMouseValues() {
    WriteSetting("mouse_enabled", Settings::values.mouse_enabled, false);

    for (int i = 0; i < Settings::NativeMouseButton::NumMouseButtons; ++i) {
        std::string default_param = InputCommon::GenerateKeyboardParam(default_mouse_buttons[i]);
        WriteSetting(QString("mouse_") +
                         QString::fromStdString(Settings::NativeMouseButton::mapping[i]),
                     QString::fromStdString(Settings::values.mouse_buttons[i]),
                     QString::fromStdString(default_param));
    }
}

void Config::SaveTouchscreenValues() {
    WriteSetting("touchscreen_enabled", Settings::values.touchscreen.enabled, true);
    WriteSetting("touchscreen_device", QString::fromStdString(Settings::values.touchscreen.device),
                 "engine:emu_window");

    WriteSetting("touchscreen_finger", Settings::values.touchscreen.finger, 0);
    WriteSetting("touchscreen_angle", Settings::values.touchscreen.rotation_angle, 0);
    WriteSetting("touchscreen_diameter_x", Settings::values.touchscreen.diameter_x, 15);
    WriteSetting("touchscreen_diameter_y", Settings::values.touchscreen.diameter_y, 15);
}

void Config::SaveValues() {
    qt_config->beginGroup("Controls");

    SavePlayerValues();
    SaveDebugValues();
    SaveMouseValues();
    SaveTouchscreenValues();

    WriteSetting("motion_device", QString::fromStdString(Settings::values.motion_device),
                 "engine:motion_emu,update_period:100,sensitivity:0.01");
    WriteSetting("keyboard_enabled", Settings::values.keyboard_enabled, false);

    qt_config->endGroup();

    qt_config->beginGroup("Core");
    WriteSetting("use_cpu_jit", Settings::values.use_cpu_jit, true);
    WriteSetting("use_multi_core", Settings::values.use_multi_core, false);
    qt_config->endGroup();

    qt_config->beginGroup("Renderer");
    WriteSetting("resolution_factor", (double)Settings::values.resolution_factor, 1.0);
    WriteSetting("use_frame_limit", Settings::values.use_frame_limit, true);
    WriteSetting("frame_limit", Settings::values.frame_limit, 100);
    WriteSetting("use_compatibility_profile", Settings::values.use_compatibility_profile, true);
    WriteSetting("use_disk_shader_cache", Settings::values.use_disk_shader_cache, true);
    WriteSetting("use_accurate_gpu_emulation", Settings::values.use_accurate_gpu_emulation, false);
    WriteSetting("use_asynchronous_gpu_emulation", Settings::values.use_asynchronous_gpu_emulation,
                 false);
    WriteSetting("force_30fps_mode", Settings::values.force_30fps_mode, false);
    WriteSetting("fps_mode", Settings::values.fps_mode, 60);

    // Cast to double because Qt's written float values are not human-readable
    WriteSetting("bg_red", (double)Settings::values.bg_red, 0.0);
    WriteSetting("bg_green", (double)Settings::values.bg_green, 0.0);
    WriteSetting("bg_blue", (double)Settings::values.bg_blue, 0.0);
    qt_config->endGroup();

    qt_config->beginGroup("Audio");
    WriteSetting("output_engine", QString::fromStdString(Settings::values.sink_id), "auto");
    WriteSetting("enable_audio_stretching", Settings::values.enable_audio_stretching, true);
    WriteSetting("output_device", QString::fromStdString(Settings::values.audio_device_id), "auto");
    WriteSetting("volume", Settings::values.volume, 1.0f);
    qt_config->endGroup();

    qt_config->beginGroup("Data Storage");
    WriteSetting("use_virtual_sd", Settings::values.use_virtual_sd, true);
    WriteSetting("nand_directory",
                 QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::NANDDir)),
                 QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::NANDDir)));
    WriteSetting("sdmc_directory",
                 QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir)),
                 QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir)));
    qt_config->endGroup();

    qt_config->beginGroup("System");
    WriteSetting("use_docked_mode", Settings::values.use_docked_mode, false);
    WriteSetting("current_user", Settings::values.current_user, 0);
    WriteSetting("language_index", Settings::values.language_index, 1);

    WriteSetting("rng_seed_enabled", Settings::values.rng_seed.has_value(), false);
    WriteSetting("rng_seed", Settings::values.rng_seed.value_or(0), 0);

    WriteSetting("custom_rtc_enabled", Settings::values.custom_rtc.has_value(), false);
    WriteSetting("custom_rtc",
                 QVariant::fromValue<long long>(
                     Settings::values.custom_rtc.value_or(std::chrono::seconds{}).count()),
                 0);

    qt_config->endGroup();

    qt_config->beginGroup("Miscellaneous");
    WriteSetting("log_filter", QString::fromStdString(Settings::values.log_filter), "*:Info");
    WriteSetting("use_dev_keys", Settings::values.use_dev_keys, false);
    qt_config->endGroup();

    qt_config->beginGroup("Debugging");
    WriteSetting("use_gdbstub", Settings::values.use_gdbstub, false);
    WriteSetting("gdbstub_port", Settings::values.gdbstub_port, 24689);
    WriteSetting("program_args", QString::fromStdString(Settings::values.program_args), "");
    WriteSetting("dump_exefs", Settings::values.dump_exefs, false);
    WriteSetting("dump_nso", Settings::values.dump_nso, false);
    qt_config->endGroup();

    qt_config->beginGroup("WebService");
    WriteSetting("enable_telemetry", Settings::values.enable_telemetry, true);
    WriteSetting("web_api_url", QString::fromStdString(Settings::values.web_api_url),
                 "https://api.yuzu-emu.org");
    WriteSetting("yuzu_username", QString::fromStdString(Settings::values.yuzu_username));
    WriteSetting("yuzu_token", QString::fromStdString(Settings::values.yuzu_token));
    qt_config->endGroup();

    qt_config->beginWriteArray("DisabledAddOns");
    int i = 0;
    for (const auto& elem : Settings::values.disabled_addons) {
        qt_config->setArrayIndex(i);
        WriteSetting("title_id", QVariant::fromValue<u64>(elem.first), 0);
        qt_config->beginWriteArray("disabled");
        for (std::size_t j = 0; j < elem.second.size(); ++j) {
            qt_config->setArrayIndex(static_cast<int>(j));
            WriteSetting("d", QString::fromStdString(elem.second[j]), "");
        }
        qt_config->endArray();
        ++i;
    }
    qt_config->endArray();

    qt_config->beginGroup("UI");
    WriteSetting("theme", UISettings::values.theme, UISettings::themes[0].second);
    WriteSetting("enable_discord_presence", UISettings::values.enable_discord_presence, true);
    WriteSetting("screenshot_resolution_factor", UISettings::values.screenshot_resolution_factor,
                 0);
    WriteSetting("select_user_on_boot", UISettings::values.select_user_on_boot, false);

    qt_config->beginGroup("UIGameList");
    WriteSetting("show_unknown", UISettings::values.show_unknown, true);
    WriteSetting("show_add_ons", UISettings::values.show_add_ons, true);
    WriteSetting("icon_size", UISettings::values.icon_size, 64);
    WriteSetting("row_1_text_id", UISettings::values.row_1_text_id, 3);
    WriteSetting("row_2_text_id", UISettings::values.row_2_text_id, 2);
    qt_config->endGroup();

    qt_config->beginGroup("UILayout");
    WriteSetting("geometry", UISettings::values.geometry);
    WriteSetting("state", UISettings::values.state);
    WriteSetting("geometryRenderWindow", UISettings::values.renderwindow_geometry);
    WriteSetting("gameListHeaderState", UISettings::values.gamelist_header_state);
    WriteSetting("microProfileDialogGeometry", UISettings::values.microprofile_geometry);
    WriteSetting("microProfileDialogVisible", UISettings::values.microprofile_visible, false);
    qt_config->endGroup();

    qt_config->beginGroup("Paths");
    WriteSetting("romsPath", UISettings::values.roms_path);
    WriteSetting("symbolsPath", UISettings::values.symbols_path);
    WriteSetting("screenshotPath", UISettings::values.screenshot_path);
    WriteSetting("gameListRootDir", UISettings::values.game_directory_path, ".");
    WriteSetting("gameListDeepScan", UISettings::values.game_directory_deepscan, false);
    WriteSetting("recentFiles", UISettings::values.recent_files);
    qt_config->endGroup();

    qt_config->beginGroup("Shortcuts");
    // Lengths of UISettings::values.shortcuts & default_hotkeys are same.
    // However, their ordering must also be the same.
    for (std::size_t i = 0; i < default_hotkeys.size(); i++) {
        auto [name, group, shortcut] = UISettings::values.shortcuts[i];
        qt_config->beginGroup(group);
        qt_config->beginGroup(name);
        WriteSetting("KeySeq", shortcut.first, default_hotkeys[i].shortcut.first);
        WriteSetting("Context", shortcut.second, default_hotkeys[i].shortcut.second);
        qt_config->endGroup();
        qt_config->endGroup();
    }
    qt_config->endGroup();

    WriteSetting("singleWindowMode", UISettings::values.single_window_mode, true);
    WriteSetting("fullscreen", UISettings::values.fullscreen, false);
    WriteSetting("displayTitleBars", UISettings::values.display_titlebar, true);
    WriteSetting("showFilterBar", UISettings::values.show_filter_bar, true);
    WriteSetting("showStatusBar", UISettings::values.show_status_bar, true);
    WriteSetting("confirmClose", UISettings::values.confirm_before_closing, true);
    WriteSetting("firstStart", UISettings::values.first_start, true);
    WriteSetting("calloutFlags", UISettings::values.callout_flags, 0);
    WriteSetting("showConsole", UISettings::values.show_console, false);
    WriteSetting("profileIndex", UISettings::values.profile_index, 0);
    qt_config->endGroup();
}

QVariant Config::ReadSetting(const QString& name) const {
    return qt_config->value(name);
}

QVariant Config::ReadSetting(const QString& name, const QVariant& default_value) const {
    QVariant result;
    if (qt_config->value(name + "/default", false).toBool()) {
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
    qt_config->setValue(name + "/default", value == default_value);
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
