// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include "common/assert.h"
#include "common/bit_field.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "common/swap.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/frontend/input.h"
#include "core/hle/kernel/event.h"
#include "core/hle/service/hid/controllers/npad.h"
#include "core/settings.h"

namespace Service::HID {
constexpr u32 JOYCON_BODY_NEON_RED = 0xFF3C28;
constexpr u32 JOYCON_BUTTONS_NEON_RED = 0x1E0A0A;
constexpr u32 JOYCON_BODY_NEON_BLUE = 0x0AB9E6;
constexpr u32 JOYCON_BUTTONS_NEON_BLUE = 0x001E1E;
constexpr s32 HID_JOYSTICK_MAX = 0x7fff;
constexpr s32 HID_JOYSTICK_MIN = -0x7fff;
constexpr std::size_t NPAD_OFFSET = 0x9A00;
constexpr u32 BATTERY_FULL = 2;
enum class JoystickId : std::size_t { Joystick_Left, Joystick_Right };

Controller_NPad::Controller_NPad() = default;

void Controller_NPad::InitNewlyAddedControler(std::size_t controller_idx) {
    const auto controller_type = connected_controllers[controller_idx].type;
    auto& controller = shared_memory_entries[controller_idx];
    if (controller_type == NPadControllerType::None) {
        return;
    }
    controller.joy_styles.raw = 0; // Zero out
    controller.device_type.raw = 0;
    switch (controller_type) {
    case NPadControllerType::Handheld:
        controller.joy_styles.handheld.Assign(1);
        controller.device_type.handheld.Assign(1);
        controller.pad_assignment = NPadAssignments::Dual;
        break;
    case NPadControllerType::JoyDual:
        controller.joy_styles.joycon_dual.Assign(1);
        controller.device_type.joycon_left.Assign(1);
        controller.device_type.joycon_right.Assign(1);
        controller.pad_assignment = NPadAssignments::Dual;
        break;
    case NPadControllerType::JoyLeft:
        controller.joy_styles.joycon_left.Assign(1);
        controller.device_type.joycon_left.Assign(1);
        controller.pad_assignment = NPadAssignments::Dual;
        break;
    case NPadControllerType::JoyRight:
        controller.joy_styles.joycon_right.Assign(1);
        controller.device_type.joycon_right.Assign(1);
        controller.pad_assignment = NPadAssignments::Dual;
        break;
    case NPadControllerType::Pokeball:
        controller.joy_styles.pokeball.Assign(1);
        controller.device_type.pokeball.Assign(1);
        controller.pad_assignment = NPadAssignments::Single;
        break;
    case NPadControllerType::ProController:
        controller.joy_styles.pro_controller.Assign(1);
        controller.device_type.pro_controller.Assign(1);
        controller.pad_assignment = NPadAssignments::Single;
        break;
    }

    controller.single_color_error = ColorReadError::ReadOk;
    controller.single_color.body_color = 0;
    controller.single_color.button_color = 0;

    controller.dual_color_error = ColorReadError::ReadOk;
    controller.left_color.body_color = JOYCON_BODY_NEON_BLUE;
    controller.left_color.button_color = JOYCON_BUTTONS_NEON_BLUE;
    controller.right_color.body_color = JOYCON_BODY_NEON_RED;
    controller.right_color.button_color = JOYCON_BUTTONS_NEON_RED;

    controller.properties.is_vertical.Assign(1); // TODO(ogniK): Swap joycons orientations
    controller.properties.use_plus.Assign(1);
    controller.properties.use_minus.Assign(1);
    controller.battery_level[0] = BATTERY_FULL;
    controller.battery_level[1] = BATTERY_FULL;
    controller.battery_level[2] = BATTERY_FULL;
}

void Controller_NPad::OnInit() {
    auto& kernel = Core::System::GetInstance().Kernel();
    styleset_changed_event =
        Kernel::Event::Create(kernel, Kernel::ResetType::OneShot, "npad:NpadStyleSetChanged");

    if (!IsControllerActivated())
        return;
    std::size_t controller{};
    if (style.raw == 0) {
        // We want to support all controllers
        style.handheld.Assign(1);
        style.joycon_left.Assign(1);
        style.joycon_right.Assign(1);
        style.joycon_dual.Assign(1);
        style.pro_controller.Assign(1);
        style.pokeball.Assign(1);
    }
    if (std::none_of(connected_controllers.begin(), connected_controllers.end(),
                     [](const ControllerHolder& controller) { return controller.is_connected; })) {
        supported_npad_id_types.resize(npad_id_list.size());
        std::memcpy(supported_npad_id_types.data(), npad_id_list.data(),
                    npad_id_list.size() * sizeof(u32));
        AddNewController(NPadControllerType::JoyDual);
    }
}

void Controller_NPad::OnLoadInputDevices() {
    std::transform(Settings::values.buttons.begin() + Settings::NativeButton::BUTTON_HID_BEGIN,
                   Settings::values.buttons.begin() + Settings::NativeButton::BUTTON_HID_END,
                   buttons.begin(), Input::CreateDevice<Input::ButtonDevice>);
    std::transform(Settings::values.analogs.begin() + Settings::NativeAnalog::STICK_HID_BEGIN,
                   Settings::values.analogs.begin() + Settings::NativeAnalog::STICK_HID_END,
                   sticks.begin(), Input::CreateDevice<Input::AnalogDevice>);
}

void Controller_NPad::OnRelease() {}

void Controller_NPad::OnUpdate(u8* data, std::size_t data_len) {
    if (!IsControllerActivated())
        return;
    for (std::size_t i = 0; i < shared_memory_entries.size(); i++) {
        auto& npad = shared_memory_entries[i];
        const std::array<NPadGeneric*, 7> controller_npads{&npad.main_controller_states,
                                                           &npad.handheld_states,
                                                           &npad.dual_states,
                                                           &npad.left_joy_states,
                                                           &npad.right_joy_states,
                                                           &npad.pokeball_states,
                                                           &npad.libnx};

        for (auto* main_controller : controller_npads) {
            main_controller->common.entry_count = 16;
            main_controller->common.total_entry_count = 17;

            const auto& last_entry =
                main_controller->npad[main_controller->common.last_entry_index];

            main_controller->common.timestamp = CoreTiming::GetTicks();
            main_controller->common.last_entry_index =
                (main_controller->common.last_entry_index + 1) % 17;

            auto& cur_entry = main_controller->npad[main_controller->common.last_entry_index];

            cur_entry.timestamp = last_entry.timestamp + 1;
            cur_entry.timestamp2 = cur_entry.timestamp;
        }

        const auto& controller_type = connected_controllers[i].type;

        if (controller_type == NPadControllerType::None || !connected_controllers[i].is_connected) {
            continue;
        }

        // Pad states
        ControllerPadState pad_state{};
        using namespace Settings::NativeButton;
        pad_state.a.Assign(buttons[A - BUTTON_HID_BEGIN]->GetStatus());
        pad_state.b.Assign(buttons[B - BUTTON_HID_BEGIN]->GetStatus());
        pad_state.x.Assign(buttons[X - BUTTON_HID_BEGIN]->GetStatus());
        pad_state.y.Assign(buttons[Y - BUTTON_HID_BEGIN]->GetStatus());
        pad_state.l_stick.Assign(buttons[LStick - BUTTON_HID_BEGIN]->GetStatus());
        pad_state.r_stick.Assign(buttons[RStick - BUTTON_HID_BEGIN]->GetStatus());
        pad_state.l.Assign(buttons[L - BUTTON_HID_BEGIN]->GetStatus());
        pad_state.r.Assign(buttons[R - BUTTON_HID_BEGIN]->GetStatus());
        pad_state.zl.Assign(buttons[ZL - BUTTON_HID_BEGIN]->GetStatus());
        pad_state.zr.Assign(buttons[ZR - BUTTON_HID_BEGIN]->GetStatus());
        pad_state.plus.Assign(buttons[Plus - BUTTON_HID_BEGIN]->GetStatus());
        pad_state.minus.Assign(buttons[Minus - BUTTON_HID_BEGIN]->GetStatus());

        pad_state.d_left.Assign(buttons[DLeft - BUTTON_HID_BEGIN]->GetStatus());
        pad_state.d_up.Assign(buttons[DUp - BUTTON_HID_BEGIN]->GetStatus());
        pad_state.d_right.Assign(buttons[DRight - BUTTON_HID_BEGIN]->GetStatus());
        pad_state.d_down.Assign(buttons[DDown - BUTTON_HID_BEGIN]->GetStatus());

        pad_state.l_stick_left.Assign(buttons[LStick_Left - BUTTON_HID_BEGIN]->GetStatus());
        pad_state.l_stick_up.Assign(buttons[LStick_Up - BUTTON_HID_BEGIN]->GetStatus());
        pad_state.l_stick_right.Assign(buttons[LStick_Right - BUTTON_HID_BEGIN]->GetStatus());
        pad_state.l_stick_down.Assign(buttons[LStick_Down - BUTTON_HID_BEGIN]->GetStatus());

        pad_state.r_stick_left.Assign(buttons[RStick_Left - BUTTON_HID_BEGIN]->GetStatus());
        pad_state.r_stick_up.Assign(buttons[RStick_Up - BUTTON_HID_BEGIN]->GetStatus());
        pad_state.r_stick_right.Assign(buttons[RStick_Right - BUTTON_HID_BEGIN]->GetStatus());
        pad_state.r_stick_down.Assign(buttons[RStick_Down - BUTTON_HID_BEGIN]->GetStatus());

        pad_state.sl.Assign(buttons[SL - BUTTON_HID_BEGIN]->GetStatus());
        pad_state.sr.Assign(buttons[SR - BUTTON_HID_BEGIN]->GetStatus());

        AnalogPosition lstick_entry{};
        AnalogPosition rstick_entry{};

        const auto [stick_l_x_f, stick_l_y_f] =
            sticks[static_cast<std::size_t>(JoystickId::Joystick_Left)]->GetStatus();
        const auto [stick_r_x_f, stick_r_y_f] =
            sticks[static_cast<std::size_t>(JoystickId::Joystick_Right)]->GetStatus();
        lstick_entry.x = static_cast<s32>(stick_l_x_f * HID_JOYSTICK_MAX);
        lstick_entry.y = static_cast<s32>(stick_l_y_f * HID_JOYSTICK_MAX);
        rstick_entry.x = static_cast<s32>(stick_r_x_f * HID_JOYSTICK_MAX);
        rstick_entry.y = static_cast<s32>(stick_r_y_f * HID_JOYSTICK_MAX);

        auto& main_controller =
            npad.main_controller_states.npad[npad.main_controller_states.common.last_entry_index];
        auto& handheld_entry =
            npad.handheld_states.npad[npad.handheld_states.common.last_entry_index];
        auto& dual_entry = npad.dual_states.npad[npad.dual_states.common.last_entry_index];
        auto& left_entry = npad.left_joy_states.npad[npad.left_joy_states.common.last_entry_index];
        auto& right_entry =
            npad.right_joy_states.npad[npad.right_joy_states.common.last_entry_index];
        auto& pokeball_entry =
            npad.pokeball_states.npad[npad.pokeball_states.common.last_entry_index];
        auto& libnx_entry = npad.libnx.npad[npad.libnx.common.last_entry_index];

        if (hold_type == NpadHoldType::Horizontal) {
            // TODO(ogniK): Remap buttons for different orientations
        }
        libnx_entry.connection_status.raw = 0;

        switch (controller_type) {
        case NPadControllerType::Handheld:
            handheld_entry.connection_status.raw = 0;
            handheld_entry.connection_status.IsConnected.Assign(1);
            if (!Settings::values.use_docked_mode) {
                handheld_entry.connection_status.IsWired.Assign(1);
            }
            handheld_entry.pad_states.raw = pad_state.raw;
            handheld_entry.l_stick = lstick_entry;
            handheld_entry.r_stick = rstick_entry;
            break;
        case NPadControllerType::JoyDual:
            dual_entry.connection_status.raw = 0;

            dual_entry.connection_status.IsLeftJoyConnected.Assign(1);
            dual_entry.connection_status.IsRightJoyConnected.Assign(1);
            dual_entry.connection_status.IsConnected.Assign(1);

            libnx_entry.connection_status.IsLeftJoyConnected.Assign(1);
            libnx_entry.connection_status.IsRightJoyConnected.Assign(1);
            libnx_entry.connection_status.IsConnected.Assign(1);

            dual_entry.pad_states.raw = pad_state.raw;
            dual_entry.l_stick = lstick_entry;
            dual_entry.r_stick = rstick_entry;
        case NPadControllerType::JoyLeft:
            left_entry.connection_status.raw = 0;

            left_entry.connection_status.IsConnected.Assign(1);
            left_entry.pad_states.raw = pad_state.raw;
            left_entry.l_stick = lstick_entry;
            left_entry.r_stick = rstick_entry;
            break;
        case NPadControllerType::JoyRight:
            right_entry.connection_status.raw = 0;

            right_entry.connection_status.IsConnected.Assign(1);
            right_entry.pad_states.raw = pad_state.raw;
            right_entry.l_stick = lstick_entry;
            right_entry.r_stick = rstick_entry;
            break;
        case NPadControllerType::Pokeball:
            pokeball_entry.connection_status.raw = 0;

            pokeball_entry.connection_status.IsConnected.Assign(1);
            pokeball_entry.connection_status.IsWired.Assign(1);

            pokeball_entry.pad_states.raw = pad_state.raw;
            pokeball_entry.l_stick = lstick_entry;
            pokeball_entry.r_stick = rstick_entry;
            break;
        case NPadControllerType::ProController:
            main_controller.connection_status.raw = 0;

            main_controller.connection_status.IsConnected.Assign(1);
            main_controller.connection_status.IsWired.Assign(1);
            main_controller.pad_states.raw = pad_state.raw;
            main_controller.l_stick = lstick_entry;
            main_controller.r_stick = rstick_entry;
            break;
        }

        // LibNX exclusively uses this section, so we always update it since LibNX doesn't activate
        // any controllers.
        libnx_entry.pad_states.raw = pad_state.raw;
        libnx_entry.l_stick = lstick_entry;
        libnx_entry.r_stick = rstick_entry;
    }
    std::memcpy(data + NPAD_OFFSET, shared_memory_entries.data(),
                shared_memory_entries.size() * sizeof(NPadEntry));
} // namespace Service::HID

void Controller_NPad::SetSupportedStyleSet(NPadType style_set) {
    style.raw = style_set.raw;
}

Controller_NPad::NPadType Controller_NPad::GetSupportedStyleSet() const {
    return style;
}

void Controller_NPad::SetSupportedNPadIdTypes(u8* data, std::size_t length) {
    ASSERT(length > 0 && (length % sizeof(u32)) == 0);
    supported_npad_id_types.clear();
    supported_npad_id_types.resize(length / sizeof(u32));
    std::memcpy(supported_npad_id_types.data(), data, length);
}

const void Controller_NPad::GetSupportedNpadIdTypes(u32* data, std::size_t max_length) {
    ASSERT(max_length < supported_npad_id_types.size());
    std::memcpy(data, supported_npad_id_types.data(), supported_npad_id_types.size());
}

std::size_t Controller_NPad::GetSupportedNPadIdTypesSize() const {
    return supported_npad_id_types.size();
}

void Controller_NPad::SetHoldType(NpadHoldType joy_hold_type) {
    hold_type = joy_hold_type;
}
Controller_NPad::NpadHoldType Controller_NPad::GetHoldType() const {
    return hold_type;
}

void Controller_NPad::SetNpadMode(u32 npad_id, NPadAssignments assignment_mode) {
    ASSERT(npad_id < shared_memory_entries.size());
    shared_memory_entries[npad_id].pad_assignment = assignment_mode;
}

void Controller_NPad::VibrateController(const std::vector<u32>& controller_ids,
                                        const std::vector<Vibration>& vibrations) {
    if (!can_controllers_vibrate) {
        return;
    }
    for (std::size_t i = 0; i < controller_ids.size(); i++) {
        std::size_t controller_pos = i;
        // Handheld controller conversion
        if (controller_pos == 32) {
            controller_pos = 8;
        }
        // Unknown controller conversion
        if (controller_pos == 16) {
            controller_pos = 9;
        }
        if (connected_controllers[controller_pos].is_connected) {
            // TODO(ogniK): Vibrate the physical controller
        }
    }
    LOG_WARNING(Service_HID, "(STUBBED) called");
    last_processed_vibration = vibrations.back();
}

Kernel::SharedPtr<Kernel::Event> Controller_NPad::GetStyleSetChangedEvent() const {
    return styleset_changed_event;
}

Controller_NPad::Vibration Controller_NPad::GetLastVibration() const {
    return last_processed_vibration;
}
void Controller_NPad::AddNewController(NPadControllerType controller) {
    if (controller == NPadControllerType::Handheld) {
        connected_controllers[8] = {controller, true};
        InitNewlyAddedControler(8);
        return;
    }
    const auto pos =
        std::find_if(connected_controllers.begin(), connected_controllers.end() - 2,
                     [](const ControllerHolder& holder) { return !holder.is_connected; });
    if (pos == connected_controllers.end() - 2) {
        LOG_ERROR(Service_HID, "Cannot connect any more controllers!");
        return;
    }
    const auto controller_id = std::distance(connected_controllers.begin(), pos);
    connected_controllers[controller_id] = {controller, true};
    InitNewlyAddedControler(controller_id);
}

void Controller_NPad::ConnectNPad(u32 npad_id) {
    if (npad_id >= connected_controllers.size())
        return;
    connected_controllers[npad_id].is_connected = true;
}

void Controller_NPad::DisconnectNPad(u32 npad_id) {
    if (npad_id >= connected_controllers.size())
        return;
    connected_controllers[npad_id].is_connected = false;
}

Controller_NPad::LedPattern Controller_NPad::GetLedPattern(u32 npad_id) {
    if (npad_id == npad_id_list.back() || npad_id == npad_id_list[npad_id_list.size() - 2]) {
        // These are controllers without led patterns
        return LedPattern{0, 0, 0, 0};
    }
    switch (npad_id) {
    case 0:
        return LedPattern{1, 0, 0, 0};
    case 1:
        return LedPattern{0, 1, 0, 0};
    case 2:
        return LedPattern{0, 0, 1, 0};
    case 3:
        return LedPattern{0, 0, 0, 1};
    case 4:
        return LedPattern{1, 0, 0, 1};
    case 5:
        return LedPattern{1, 0, 1, 0};
    case 6:
        return LedPattern{1, 0, 1, 1};
    case 7:
        return LedPattern{0, 1, 1, 0};
    default:
        UNIMPLEMENTED_MSG("Unhandled npad_id {}", npad_id);
        return LedPattern{0, 0, 0, 0};
    };
}
void Controller_NPad::SetVibrationEnabled(bool can_vibrate) {
    can_controllers_vibrate = can_vibrate;
}
} // namespace Service::HID
