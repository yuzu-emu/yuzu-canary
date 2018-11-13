// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include "common/common_types.h"
#include "core/core_timing.h"
#include "core/hle/service/hid/controllers/keyboard.h"
#include "core/settings.h"

namespace Service::HID {
constexpr std::size_t SHARED_MEMORY_OFFSET = 0x3800;
constexpr u8 KEYS_PER_BYTE = 8;

Controller_Keyboard::Controller_Keyboard() = default;
Controller_Keyboard::~Controller_Keyboard() = default;

void Controller_Keyboard::OnInit() {}

void Controller_Keyboard::OnRelease() {}

void Controller_Keyboard::OnUpdate(u8* data, std::size_t size) {
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

    for (std::size_t i = 0; i < keyboard_keys.size(); ++i) {
        for (std::size_t k = 0; k < KEYS_PER_BYTE; ++k) {
            cur_entry.key[i / KEYS_PER_BYTE] |= (keyboard_keys[i]->GetStatus() << k);
        }
    }

    for (std::size_t i = 0; i < keyboard_mods.size(); ++i) {
        cur_entry.modifier |= (keyboard_mods[i]->GetStatus() << i);
    }

    std::memcpy(data + SHARED_MEMORY_OFFSET, &shared_memory, sizeof(SharedMemory));
}

void Controller_Keyboard::OnLoadInputDevices() {
    std::transform(Settings::values.keyboard_keys.begin(), Settings::values.keyboard_keys.end(),
                   keyboard_keys.begin(), Input::CreateDevice<Input::ButtonDevice>);
    std::transform(Settings::values.keyboard_mods.begin(), Settings::values.keyboard_mods.end(),
                   keyboard_mods.begin(), Input::CreateDevice<Input::ButtonDevice>);
}
} // namespace Service::HID
