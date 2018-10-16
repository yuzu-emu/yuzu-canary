// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include "common/common_types.h"
#include "common/swap.h"
#include "core/core_timing.h"
#include "core/hle/service/hid/controllers/debug_pad.h"

namespace Service::HID {

Controller_DebugPad::Controller_DebugPad() = default;

void Controller_DebugPad::OnInit() {}

void Controller_DebugPad::OnRelease() {}

void Controller_DebugPad::OnUpdate(u8* data, std::size_t size) {
    shared_memory.header.timestamp = CoreTiming::GetTicks();
    shared_memory.header.total_entry_count = 17;

    if (!IsControllerActivated()) {
        shared_memory.header.entry_count = 0;
        shared_memory.header.last_entry_index = 0;
        return;
    }
    shared_memory.header.entry_count = 16;

    const auto& last_entry = shared_memory.pad_states[shared_memory.header.last_entry_index];
    shared_memory.header.last_entry_index = (shared_memory.header.last_entry_index + 1) % 17;
    auto& cur_entry = shared_memory.pad_states[shared_memory.header.last_entry_index];

    cur_entry.sampling_number = last_entry.sampling_number + 1;
    cur_entry.sampling_number2 = cur_entry.sampling_number;
    // TODO(ogniK): Update debug pad states

    std::memcpy(data, &shared_memory, sizeof(SharedMemory));
}

void Controller_DebugPad::OnLoadInputDevices() {}
} // namespace Service::HID
