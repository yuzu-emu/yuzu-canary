// Copyright 2019 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "core/core_timing.h"
#include "core/hle/service/apm/controller.h"
#include "core/settings.h"

namespace Service::APM {

constexpr PerformanceConfiguration DEFAULT_PERFORMANCE_CONFIGURATION =
    PerformanceConfiguration::Config7;

Controller::Controller(Core::Timing::CoreTiming& core_timing)
    : core_timing(core_timing), configs{
                                    {PerformanceMode::Handheld, DEFAULT_PERFORMANCE_CONFIGURATION},
                                    {PerformanceMode::Docked, DEFAULT_PERFORMANCE_CONFIGURATION},
                                } {}

Controller::~Controller() = default;

void Controller::SetPerformanceConfiguration(PerformanceMode mode,
                                             PerformanceConfiguration config) {
    static const std::map<PerformanceConfiguration, u32> PCONFIG_TO_SPEED_MAP{
        {PerformanceConfiguration::Config1, 1020},  {PerformanceConfiguration::Config2, 1020},
        {PerformanceConfiguration::Config3, 1224},  {PerformanceConfiguration::Config4, 1020},
        {PerformanceConfiguration::Config5, 1020},  {PerformanceConfiguration::Config6, 1224},
        {PerformanceConfiguration::Config7, 1020},  {PerformanceConfiguration::Config8, 1020},
        {PerformanceConfiguration::Config9, 1020},  {PerformanceConfiguration::Config10, 1020},
        {PerformanceConfiguration::Config11, 1020}, {PerformanceConfiguration::Config12, 1020},
        {PerformanceConfiguration::Config13, 1785}, {PerformanceConfiguration::Config14, 1785},
        {PerformanceConfiguration::Config15, 1020}, {PerformanceConfiguration::Config16, 1020},
    };

    SetClockSpeed(PCONFIG_TO_SPEED_MAP.find(config)->second);
    configs.insert_or_assign(mode, config);
}

void Controller::SetFromCpuBoostMode(CpuBoostMode mode) {
    constexpr std::array<PerformanceConfiguration, 3> BOOST_MODE_TO_CONFIG_MAP{{
        PerformanceConfiguration::Config7,
        PerformanceConfiguration::Config13,
        PerformanceConfiguration::Config15,
    }};

    SetPerformanceConfiguration(PerformanceMode::Docked,
                                BOOST_MODE_TO_CONFIG_MAP.at(static_cast<u32>(mode)));
}

PerformanceMode Controller::GetCurrentPerformanceMode() {
    return Settings::values.use_docked_mode ? PerformanceMode::Docked : PerformanceMode::Handheld;
}

PerformanceConfiguration Controller::GetCurrentPerformanceConfiguration(PerformanceMode mode) {
    if (configs.find(mode) == configs.end()) {
        configs.insert_or_assign(mode, DEFAULT_PERFORMANCE_CONFIGURATION);
    }

    return configs[mode];
}

void Controller::SetClockSpeed(u32 mhz) {
    LOG_INFO(Service_APM, "called, mhz={:08X}", mhz);
    // TODO(DarkLordZach): Actually signal core_timing to change clock speed.
}

} // namespace Service::APM
