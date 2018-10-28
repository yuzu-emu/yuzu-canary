// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <string>

class INIReader;

class Config {
    std::unique_ptr<INIReader> sdl2_config;
    std::string sdl2_config_loc;
    std::vector<u64> titles;

    bool LoadINI(const std::string& default_contents = "", bool retry = true);
    void ReadValues();
    void ReadValuesForTitleID(Settings::PerGameValues& values, const std::string& name);

    bool UpdateCurrentGame(u64 title_id, Settings::PerGameValues& values);

public:
    Config();
    ~Config();

    void Reload();
};
