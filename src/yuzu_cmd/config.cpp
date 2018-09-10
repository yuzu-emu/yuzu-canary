// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <SDL.h>
#include <inih/cpp/INIReader.h>
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/param_package.h"
#include "core/settings.h"
#include "input_common/main.h"
#include "yuzu_cmd/config.h"
#include "yuzu_cmd/default_ini.h"

Config::Config() {
    // TODO: Don't hardcode the path; let the frontend decide where to put the config files.
    sdl2_config_loc = FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) + "sdl2-config.ini";
    sdl2_config = std::make_unique<INIReader>(sdl2_config_loc);

    Reload();
}

Config::~Config() = default;

bool Config::LoadINI(const std::string& default_contents, bool retry) {
    const char* location = this->sdl2_config_loc.c_str();
    if (sdl2_config->ParseError() < 0) {
        if (retry) {
            LOG_WARNING(Config, "Failed to load {}. Creating file from defaults...", location);
            FileUtil::CreateFullPath(location);
            FileUtil::WriteStringToFile(true, default_contents, location);
            sdl2_config = std::make_unique<INIReader>(location); // Reopen file

            return LoadINI(default_contents, false);
        }
        LOG_ERROR(Config, "Failed.");
        return false;
    }
    LOG_INFO(Config, "Successfully loaded {}", location);
    return true;
}

static const std::array<int, Settings::NativeButton::NumButtons> default_buttons = {
    SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_T,
    SDL_SCANCODE_G, SDL_SCANCODE_F, SDL_SCANCODE_H, SDL_SCANCODE_Q, SDL_SCANCODE_W,
    SDL_SCANCODE_M, SDL_SCANCODE_N, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_B,
};

static const std::array<std::array<int, 5>, Settings::NativeAnalog::NumAnalogs> default_analogs{{
    {
        SDL_SCANCODE_UP,
        SDL_SCANCODE_DOWN,
        SDL_SCANCODE_LEFT,
        SDL_SCANCODE_RIGHT,
        SDL_SCANCODE_D,
    },
    {
        SDL_SCANCODE_I,
        SDL_SCANCODE_K,
        SDL_SCANCODE_J,
        SDL_SCANCODE_L,
        SDL_SCANCODE_D,
    },
}};

void Config::ReadValues() {
    // Controls
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
        Settings::values.buttons[i] =
            sdl2_config->Get("Controls", Settings::NativeButton::mapping[i], default_param);
        if (Settings::values.buttons[i].empty())
            Settings::values.buttons[i] = default_param;
    }

    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
            default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
            default_analogs[i][3], default_analogs[i][4], 0.5f);
        Settings::values.analogs[i] =
            sdl2_config->Get("Controls", Settings::NativeAnalog::mapping[i], default_param);
        if (Settings::values.analogs[i].empty())
            Settings::values.analogs[i] = default_param;
    }

    Settings::values.motion_device = sdl2_config->Get(
        "Controls", "motion_device", "engine:motion_emu,update_period:100,sensitivity:0.01");
    Settings::values.touch_device =
        sdl2_config->Get("Controls", "touch_device", "engine:emu_window");

    // Core
    Settings::values.use_cpu_jit = sdl2_config->GetBoolean("Core", "use_cpu_jit", true);
    Settings::values.use_multi_core = sdl2_config->GetBoolean("Core", "use_multi_core", false);

    // Renderer
    Settings::values.resolution_factor =
        (float)sdl2_config->GetReal("Renderer", "resolution_factor", 1.0);
    Settings::values.use_frame_limit = sdl2_config->GetBoolean("Renderer", "use_frame_limit", true);
    Settings::values.frame_limit =
        static_cast<u16>(sdl2_config->GetInteger("Renderer", "frame_limit", 100));
    Settings::values.use_accurate_framebuffers =
        sdl2_config->GetBoolean("Renderer", "use_accurate_framebuffers", false);

    Settings::values.bg_red = (float)sdl2_config->GetReal("Renderer", "bg_red", 0.0);
    Settings::values.bg_green = (float)sdl2_config->GetReal("Renderer", "bg_green", 0.0);
    Settings::values.bg_blue = (float)sdl2_config->GetReal("Renderer", "bg_blue", 0.0);

    // Audio
    Settings::values.sink_id = sdl2_config->Get("Audio", "output_engine", "auto");
    Settings::values.enable_audio_stretching =
        sdl2_config->GetBoolean("Audio", "enable_audio_stretching", true);
    Settings::values.audio_device_id = sdl2_config->Get("Audio", "output_device", "auto");
    Settings::values.volume = sdl2_config->GetReal("Audio", "volume", 1);

    // Data Storage
    Settings::values.use_virtual_sd =
        sdl2_config->GetBoolean("Data Storage", "use_virtual_sd", true);
    FileUtil::GetUserPath(FileUtil::UserPath::NANDDir,
                          sdl2_config->Get("Data Storage", "nand_directory",
                                           FileUtil::GetUserPath(FileUtil::UserPath::NANDDir)));
    FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir,
                          sdl2_config->Get("Data Storage", "nand_directory",
                                           FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir)));

    // System
    Settings::values.use_docked_mode = sdl2_config->GetBoolean("System", "use_docked_mode", false);

    // Miscellaneous
    Settings::values.log_filter = sdl2_config->Get("Miscellaneous", "log_filter", "*:Trace");
    Settings::values.use_dev_keys = sdl2_config->GetBoolean("Miscellaneous", "use_dev_keys", false);

    // Debugging
    Settings::values.use_gdbstub = sdl2_config->GetBoolean("Debugging", "use_gdbstub", false);
    Settings::values.gdbstub_port =
        static_cast<u16>(sdl2_config->GetInteger("Debugging", "gdbstub_port", 24689));
}

void Config::Reload() {
    LoadINI(DefaultINI::sdl2_config_file);
    ReadValues();
}
