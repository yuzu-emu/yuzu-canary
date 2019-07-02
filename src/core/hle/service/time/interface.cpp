// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/hle/service/time/interface.h"

namespace Service::Time {

Time::Time(std::shared_ptr<Module> time, std::shared_ptr<SharedMemory> shared_memory,
           const char* name)
    : Module::Interface(std::move(time), std::move(shared_memory), name) {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &Time::GetStandardUserSystemClock, "GetStandardUserSystemClock"},
        {1, &Time::GetStandardNetworkSystemClock, "GetStandardNetworkSystemClock"},
        {2, &Time::GetStandardSteadyClock, "GetStandardSteadyClock"},
        {3, &Time::GetTimeZoneService, "GetTimeZoneService"},
        {4, &Time::GetStandardLocalSystemClock, "GetStandardLocalSystemClock"},
        {5, nullptr, "GetEphemeralNetworkSystemClock"},
        {20, &Time::GetSharedMemoryNativeHandle, "GetSharedMemoryNativeHandle"},
        {30, nullptr, "GetStandardNetworkClockOperationEventReadableHandle"},
        {31, nullptr, "GetEphemeralNetworkClockOperationEventReadableHandle"},
        {50, nullptr, "SetStandardSteadyClockInternalOffset"},
        {100, &Time::IsStandardUserSystemClockAutomaticCorrectionEnabled, "IsStandardUserSystemClockAutomaticCorrectionEnabled"},
        {101, &Time::SetStandardUserSystemClockAutomaticCorrectionEnabled, "SetStandardUserSystemClockAutomaticCorrectionEnabled"},
        {102, nullptr, "GetStandardUserSystemClockInitialYear"},
        {200, nullptr, "IsStandardNetworkSystemClockAccuracySufficient"},
        {201, nullptr, "GetStandardUserSystemClockAutomaticCorrectionUpdatedTime"},
        {300, nullptr, "CalculateMonotonicSystemClockBaseTimePoint"},
        {400, &Time::GetClockSnapshot, "GetClockSnapshot"},
        {401, nullptr, "GetClockSnapshotFromSystemClockContext"},
        {500, &Time::CalculateStandardUserSystemClockDifferenceByUser, "CalculateStandardUserSystemClockDifferenceByUser"},
        {501, nullptr, "CalculateSpanBetween"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

Time::~Time() = default;

} // namespace Service::Time
