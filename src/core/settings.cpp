// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/core.h"
#include "core/gdbstub/gdbstub.h"
#include "core/hle/service/hid/hid.h"
#include "core/settings.h"
#include "video_core/renderer_base.h"

namespace Settings {

Values values = {};

void PerGameValues::LogSettings() {
    LOG_INFO(Config, "Buttons:");
    for (std::size_t i = 0; i < NativeButton::NumButtons; ++i)
        LOG_INFO(Config, "  Button[{}]: ", buttons[i]);

    LOG_INFO(Config, "Analogs:");
    for (std::size_t i = 0; i < NativeAnalog::NumAnalogs; ++i)
        LOG_INFO(Config, "  Analog[{}]: ", analogs[i]);

    LOG_INFO(Config, "Motion Device: {}", motion_device);
    LOG_INFO(Config, "Touch Device: {}", touch_device);

    LOG_INFO(Config, "Docked Mode: {}", use_docked_mode);

    LOG_INFO(Config, "Resolution Factor: {}", resolution_factor);
    LOG_INFO(Config, "Frame Limit Enabled: {}", use_frame_limit);
    LOG_INFO(Config, "Frame Limit: {}", frame_limit);

    LOG_INFO(Config, "Background Red: {}", bg_red);
    LOG_INFO(Config, "Background Green: {}", bg_green);
    LOG_INFO(Config, "Background Blue: {}", bg_blue);

    LOG_INFO(Config, "Audio Sink ID: {}", sink_id);
    LOG_INFO(Config, "Audio Stretching: {}", enable_audio_stretching);
    LOG_INFO(Config, "Audio Device ID: {}", audio_device_id);
    LOG_INFO(Config, "Volume: {}", volume);

    LOG_INFO(Config, "Program Arguments: {}", program_args);

    LOG_INFO(Config, "Disabled Patches:");
    for (const auto& patch : disabled_patches)
        LOG_INFO(Config, "  - {}", patch);
}

Values::Values() : current_game(default_game) {}

void Values::SetUpdateCurrentGameFunction(std::function<bool(u64, PerGameValues&)> new_function) {
    update_current_game = std::move(new_function);
}

u64 Values::CurrentTitleID() const {
    return current_title_id;
}

void Values::SetCurrentTitleID(u64 title_id) {
    // Ignore Updates
    title_id &= ~0x800;

    if (current_title_id == title_id)
        return;

    update_current_game(title_id, current_game);
    current_title_id = title_id;
}

PerGameValues& Values::operator[](u64 title_id) {
    // Ignore Updates
    title_id &= ~0x800;

    if (current_title_id == title_id)
        return current_game;

    if (!update_current_game(title_id, current_game))
        return default_game;

    current_title_id = title_id;
    return current_game;
}

const PerGameValues& Values::operator[](u64 title_id) const {
    // Ignore Updates
    title_id &= ~0x800;

    if (current_title_id == title_id)
        return current_game;

    return default_game;
}

PerGameValues* Values::operator->() {
    if (current_title_id == 0)
        return &default_game;

    return &current_game;
}

const PerGameValues* Values::operator->() const {
    if (current_title_id == 0)
        return &default_game;

    return &current_game;
}

PerGameValues& Values::operator*() {
    if (current_title_id == 0)
        return default_game;
    return current_game;
}

const PerGameValues& Values::operator*() const {
    if (current_title_id == 0)
        return default_game;
    return current_game;
}

void Apply() {
    GDBStub::SetServerPort(values.gdbstub_port);
    GDBStub::ToggleServer(values.use_gdbstub);

    auto& system_instance = Core::System::GetInstance();
    if (system_instance.IsPoweredOn()) {
        system_instance.Renderer().RefreshBaseSettings();
    }

    Service::HID::ReloadInputDevices();
}

} // namespace Settings
