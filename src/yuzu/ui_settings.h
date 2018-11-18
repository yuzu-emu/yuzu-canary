// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <atomic>
#include <vector>
#include <QByteArray>
#include <QString>
#include <QStringList>

namespace UISettings {

using ContextualShortcut = std::pair<QString, int>;
using Shortcut = std::pair<QString, ContextualShortcut>;

using Themes = std::array<std::pair<const char*, const char*>, 2>;
extern const Themes themes;

struct Values {
    QByteArray geometry;
    QByteArray state;

    QByteArray renderwindow_geometry;

    QByteArray gamelist_header_state;

    QByteArray microprofile_geometry;
    bool microprofile_visible;

    bool single_window_mode;
    bool fullscreen;
    bool display_titlebar;
    bool show_filter_bar;
    bool show_status_bar;

    bool confirm_before_closing;
    bool first_start;

    // Discord RPC
    bool enable_discord_presence;

    QString roms_path;
    QString symbols_path;
    QString gamedir;
    bool gamedir_deepscan;
    QStringList recent_files;

    QString theme;

    // Shortcut name <Shortcut, context>
    std::vector<Shortcut> shortcuts;

    uint32_t callout_flags;

    // logging
    bool show_console;

    // Game List
    bool show_unknown;
    bool show_add_ons;
    uint32_t icon_size;
    uint8_t row_1_text_id;
    uint8_t row_2_text_id;
    std::atomic_bool is_game_list_reload_pending{false};
};

extern Values values;
} // namespace UISettings
