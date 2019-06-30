// Copyright 2019 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/core.h"
#include "core/hle/service/time/time_sharedmemory.h"

namespace Service::Time {
const std::size_t SHARED_MEMORY_SIZE = 0x1000;

SharedMemory::SharedMemory(Core::System& system) : system(system) {
    shared_memory_holder = Kernel::SharedMemory::Create(
        system.Kernel(), nullptr, SHARED_MEMORY_SIZE, Kernel::MemoryPermission::ReadWrite,
        Kernel::MemoryPermission::Read, 0, Kernel::MemoryRegion::BASE, "Time:SharedMemory");

    // Seems static from 1.0.0 -> 8.1.0. Specific games seem to check this value and crash
    // if it's set to anything else
    shared_memory_format.format_version = 14;
    std::memcpy(shared_memory_holder->GetPointer(), &shared_memory_format, sizeof(Format));
}

SharedMemory::~SharedMemory() = default;

Kernel::SharedPtr<Kernel::SharedMemory> SharedMemory::GetSharedMemoryHolder() const {
    return shared_memory_holder;
}

void SharedMemory::SetStandardSteadyClockTimepoint(const SteadyClockTimePoint& timepoint) {
    shared_memory_format.standard_steady_clock_timepoint.StoreData(
        shared_memory_holder->GetPointer(), timepoint);
}

void SharedMemory::SetStandardLocalSystemClockContext(const SystemClockContext& context) {
    shared_memory_format.standard_local_system_clock_context.StoreData(
        shared_memory_holder->GetPointer(), context);
}

void SharedMemory::SetStandardNetworkSystemClockContext(const SystemClockContext& context) {
    shared_memory_format.standard_network_system_clock_context.StoreData(
        shared_memory_holder->GetPointer(), context);
}

void SharedMemory::SetStandardUserSystemClockAutomaticCorrectionEnabled(bool enabled) {
    shared_memory_format.standard_user_system_clock_automatic_correction.StoreData(
        shared_memory_holder->GetPointer(), enabled);
}

SteadyClockTimePoint SharedMemory::GetStandardSteadyClockTimepoint() {
    return shared_memory_format.standard_steady_clock_timepoint.ReadData(
        shared_memory_holder->GetPointer());
}

SystemClockContext SharedMemory::GetStandardLocalSystemClockContext() {
    return shared_memory_format.standard_local_system_clock_context.ReadData(
        shared_memory_holder->GetPointer());
}

SystemClockContext SharedMemory::GetStandardNetworkSystemClockContext() {
    return shared_memory_format.standard_network_system_clock_context.ReadData(
        shared_memory_holder->GetPointer());
}

bool SharedMemory::GetStandardUserSystemClockAutomaticCorrectionEnabled() {
    return shared_memory_format.standard_user_system_clock_automatic_correction.ReadData(
        shared_memory_holder->GetPointer());
}

} // namespace Service::Time
