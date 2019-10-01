// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstring>
#include "common/assert.h"
#include "common/bit_field.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/frontend/input.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/readable_event.h"
#include "core/hle/kernel/writable_event.h"
#include "core/hle/service/hid/controllers/npad.h"
#include "core/settings.h"

namespace Service::HID {
constexpr s32 HID_JOYSTICK_MAX = 0x7fff;
constexpr s32 HID_JOYSTICK_MIN = -0x7fff;
constexpr std::size_t NPAD_OFFSET = 0x9A00;
constexpr u32 BATTERY_FULL = 2;
constexpr u32 MAX_NPAD_ID = 7;
constexpr std::array<u32, 10> npad_id_list{
    0, 1, 2, 3, 4, 5, 6, 7, NPAD_HANDHELD, NPAD_UNKNOWN,
};

enum class JoystickId : std::size_t {
    Joystick_Left,
    Joystick_Right,
};

static Controller_NPad::NPadControllerType MapSettingsTypeToNPad(Settings::ControllerType type) {
    switch (type) {
    case Settings::ControllerType::ProController:
        return Controller_NPad::NPadControllerType::ProController;
    case Settings::ControllerType::DualJoycon:
        return Controller_NPad::NPadControllerType::JoyDual;
    case Settings::ControllerType::LeftJoycon:
        return Controller_NPad::NPadControllerType::JoyLeft;
    case Settings::ControllerType::RightJoycon:
        return Controller_NPad::NPadControllerType::JoyRight;
    default:
        UNREACHABLE();
        return Controller_NPad::NPadControllerType::JoyDual;
    }
}

std::size_t Controller_NPad::NPadIdToIndex(u32 npad_id) {
    switch (npad_id) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
        return npad_id;
    case 8:
    case NPAD_HANDHELD:
        return 8;
    case 9:
    case NPAD_UNKNOWN:
        return 9;
    default:
        UNIMPLEMENTED_MSG("Unknown npad id {}", npad_id);
        return 0;
    }
}

u32 Controller_NPad::IndexToNPad(std::size_t index) {
    switch (index) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
        return static_cast<u32>(index);
    case 8:
        return NPAD_HANDHELD;
    case 9:
        return NPAD_UNKNOWN;
    default:
        UNIMPLEMENTED_MSG("Unknown npad index {}", index);
        return 0;
    };
}

Controller_NPad::Controller_NPad(Core::System& system) : ControllerBase(system), system(system) {}
Controller_NPad::~Controller_NPad() = default;

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
        controller.properties.is_vertical.Assign(1);
        controller.properties.use_plus.Assign(1);
        controller.properties.use_minus.Assign(1);
        break;
    case NPadControllerType::JoyDual:
        controller.joy_styles.joycon_dual.Assign(1);
        controller.device_type.joycon_left.Assign(1);
        controller.device_type.joycon_right.Assign(1);
        controller.properties.is_vertical.Assign(1);
        controller.properties.use_plus.Assign(1);
        controller.properties.use_minus.Assign(1);
        controller.pad_assignment = NPadAssignments::Dual;
        break;
    case NPadControllerType::JoyLeft:
        controller.joy_styles.joycon_left.Assign(1);
        controller.device_type.joycon_left.Assign(1);
        controller.properties.is_horizontal.Assign(1);
        controller.properties.use_minus.Assign(1);
        controller.pad_assignment = NPadAssignments::Single;
        break;
    case NPadControllerType::JoyRight:
        controller.joy_styles.joycon_right.Assign(1);
        controller.device_type.joycon_right.Assign(1);
        controller.properties.is_horizontal.Assign(1);
        controller.properties.use_plus.Assign(1);
        controller.pad_assignment = NPadAssignments::Single;
        break;
    case NPadControllerType::Pokeball:
        controller.joy_styles.pokeball.Assign(1);
        controller.device_type.pokeball.Assign(1);
        controller.pad_assignment = NPadAssignments::Single;
        break;
    case NPadControllerType::ProController:
        controller.joy_styles.pro_controller.Assign(1);
        controller.device_type.pro_controller.Assign(1);
        controller.properties.is_vertical.Assign(1);
        controller.properties.use_plus.Assign(1);
        controller.properties.use_minus.Assign(1);
        controller.pad_assignment = NPadAssignments::Single;
        break;
    }

    controller.single_color_error = ColorReadError::ReadOk;
    controller.single_color.body_color = 0;
    controller.single_color.button_color = 0;

    controller.dual_color_error = ColorReadError::ReadOk;
    controller.left_color.body_color = Settings::values.players[controller_idx].body_color_left;
    controller.left_color.button_color = Settings::values.players[controller_idx].button_color_left;
    controller.right_color.body_color = Settings::values.players[controller_idx].body_color_right;
    controller.right_color.button_color =
        Settings::values.players[controller_idx].button_color_right;

    controller.battery_level[0] = BATTERY_FULL;
    controller.battery_level[1] = BATTERY_FULL;
    controller.battery_level[2] = BATTERY_FULL;
    styleset_changed_events[controller_idx].writable->Signal();
}

void Controller_NPad::OnInit() {
    auto& kernel = system.Kernel();
    for (std::size_t i = 0; i < styleset_changed_events.size(); i++) {
        styleset_changed_events[i] = Kernel::WritableEvent::CreateEventPair(
            kernel, Kernel::ResetType::Manual, fmt::format("npad:NpadStyleSetChanged_{}", i));
    }

    if (!IsControllerActivated()) {
        return;
    }

    if (style.raw == 0) {
        // We want to support all controllers
        style.handheld.Assign(1);
        style.joycon_left.Assign(1);
        style.joycon_right.Assign(1);
        style.joycon_dual.Assign(1);
        style.pro_controller.Assign(1);
        style.pokeball.Assign(1);
    }

    std::transform(
        Settings::values.players.begin(), Settings::values.players.end(),
        connected_controllers.begin(), [](const Settings::PlayerInput& player) {
            return ControllerHolder{MapSettingsTypeToNPad(player.type), player.connected};
        });

    std::stable_partition(connected_controllers.begin(), connected_controllers.begin() + 8,
                          [](const ControllerHolder& holder) { return holder.is_connected; });

    // Account for handheld
    if (connected_controllers[8].is_connected)
        connected_controllers[8].type = NPadControllerType::Handheld;

    supported_npad_id_types.resize(npad_id_list.size());
    std::memcpy(supported_npad_id_types.data(), npad_id_list.data(),
                npad_id_list.size() * sizeof(u32));

    // Add a default dual joycon controller if none are present.
    if (std::none_of(connected_controllers.begin(), connected_controllers.end(),
                     [](const ControllerHolder& controller) { return controller.is_connected; })) {
        supported_npad_id_types.resize(npad_id_list.size());
        std::memcpy(supported_npad_id_types.data(), npad_id_list.data(),
                    npad_id_list.size() * sizeof(u32));
        AddNewController(NPadControllerType::JoyDual);
    }

    for (std::size_t i = 0; i < connected_controllers.size(); ++i) {
        const auto& controller = connected_controllers[i];
        if (controller.is_connected) {
            AddNewControllerAt(controller.type, IndexToNPad(i));
        }
    }
}

void Controller_NPad::OnLoadInputDevices() {
    const auto& players = Settings::values.players;
    for (std::size_t i = 0; i < players.size(); ++i) {
        std::transform(players[i].buttons.begin() + Settings::NativeButton::BUTTON_HID_BEGIN,
                       players[i].buttons.begin() + Settings::NativeButton::BUTTON_HID_END,
                       buttons[i].begin(), Input::CreateDevice<Input::ButtonDevice>);
        std::transform(players[i].analogs.begin() + Settings::NativeAnalog::STICK_HID_BEGIN,
                       players[i].analogs.begin() + Settings::NativeAnalog::STICK_HID_END,
                       sticks[i].begin(), Input::CreateDevice<Input::AnalogDevice>);
    }
}

void Controller_NPad::OnRelease() {}

void Controller_NPad::RequestPadStateUpdate(u32 npad_id) {
    const auto controller_idx = NPadIdToIndex(npad_id);
    const auto controller_type = connected_controllers[controller_idx].type;
    if (!connected_controllers[controller_idx].is_connected) {
        return;
    }
    auto& pad_state = npad_pad_states[controller_idx].pad_states;
    auto& lstick_entry = npad_pad_states[controller_idx].l_stick;
    auto& rstick_entry = npad_pad_states[controller_idx].r_stick;
    const auto& button_state = buttons[controller_idx];
    const auto& analog_state = sticks[controller_idx];

    using namespace Settings::NativeButton;
    pad_state.a.Assign(button_state[A - BUTTON_HID_BEGIN]->GetStatus());
    pad_state.b.Assign(button_state[B - BUTTON_HID_BEGIN]->GetStatus());
    pad_state.x.Assign(button_state[X - BUTTON_HID_BEGIN]->GetStatus());
    pad_state.y.Assign(button_state[Y - BUTTON_HID_BEGIN]->GetStatus());
    pad_state.l_stick.Assign(button_state[LStick - BUTTON_HID_BEGIN]->GetStatus());
    pad_state.r_stick.Assign(button_state[RStick - BUTTON_HID_BEGIN]->GetStatus());
    pad_state.l.Assign(button_state[L - BUTTON_HID_BEGIN]->GetStatus());
    pad_state.r.Assign(button_state[R - BUTTON_HID_BEGIN]->GetStatus());
    pad_state.zl.Assign(button_state[ZL - BUTTON_HID_BEGIN]->GetStatus());
    pad_state.zr.Assign(button_state[ZR - BUTTON_HID_BEGIN]->GetStatus());
    pad_state.plus.Assign(button_state[Plus - BUTTON_HID_BEGIN]->GetStatus());
    pad_state.minus.Assign(button_state[Minus - BUTTON_HID_BEGIN]->GetStatus());

    pad_state.d_left.Assign(button_state[DLeft - BUTTON_HID_BEGIN]->GetStatus());
    pad_state.d_up.Assign(button_state[DUp - BUTTON_HID_BEGIN]->GetStatus());
    pad_state.d_right.Assign(button_state[DRight - BUTTON_HID_BEGIN]->GetStatus());
    pad_state.d_down.Assign(button_state[DDown - BUTTON_HID_BEGIN]->GetStatus());

    pad_state.l_stick_left.Assign(button_state[LStick_Left - BUTTON_HID_BEGIN]->GetStatus());
    pad_state.l_stick_up.Assign(button_state[LStick_Up - BUTTON_HID_BEGIN]->GetStatus());
    pad_state.l_stick_right.Assign(button_state[LStick_Right - BUTTON_HID_BEGIN]->GetStatus());
    pad_state.l_stick_down.Assign(button_state[LStick_Down - BUTTON_HID_BEGIN]->GetStatus());

    pad_state.r_stick_left.Assign(button_state[RStick_Left - BUTTON_HID_BEGIN]->GetStatus());
    pad_state.r_stick_up.Assign(button_state[RStick_Up - BUTTON_HID_BEGIN]->GetStatus());
    pad_state.r_stick_right.Assign(button_state[RStick_Right - BUTTON_HID_BEGIN]->GetStatus());
    pad_state.r_stick_down.Assign(button_state[RStick_Down - BUTTON_HID_BEGIN]->GetStatus());

    pad_state.left_sl.Assign(button_state[SL - BUTTON_HID_BEGIN]->GetStatus());
    pad_state.left_sr.Assign(button_state[SR - BUTTON_HID_BEGIN]->GetStatus());

    const auto [stick_l_x_f, stick_l_y_f] =
        analog_state[static_cast<std::size_t>(JoystickId::Joystick_Left)]->GetStatus();
    const auto [stick_r_x_f, stick_r_y_f] =
        analog_state[static_cast<std::size_t>(JoystickId::Joystick_Right)]->GetStatus();
    lstick_entry.x = static_cast<s32>(stick_l_x_f * HID_JOYSTICK_MAX);
    lstick_entry.y = static_cast<s32>(stick_l_y_f * HID_JOYSTICK_MAX);
    rstick_entry.x = static_cast<s32>(stick_r_x_f * HID_JOYSTICK_MAX);
    rstick_entry.y = static_cast<s32>(stick_r_y_f * HID_JOYSTICK_MAX);
}

void Controller_NPad::OnUpdate(const Core::Timing::CoreTiming& core_timing, u8* data,
                               std::size_t data_len) {
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

            main_controller->common.timestamp = core_timing.GetTicks();
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
        const u32 npad_index = static_cast<u32>(i);
        RequestPadStateUpdate(npad_index);
        auto& pad_state = npad_pad_states[npad_index];

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

        libnx_entry.connection_status.raw = 0;

        switch (controller_type) {
        case NPadControllerType::Handheld:
            handheld_entry.connection_status.raw = 0;
            handheld_entry.connection_status.IsWired.Assign(1);
            handheld_entry.connection_status.IsLeftJoyConnected.Assign(1);
            handheld_entry.connection_status.IsRightJoyConnected.Assign(1);
            handheld_entry.connection_status.IsLeftJoyWired.Assign(1);
            handheld_entry.connection_status.IsRightJoyWired.Assign(1);
            handheld_entry.pad.pad_states.raw = pad_state.pad_states.raw;
            handheld_entry.pad.l_stick = pad_state.l_stick;
            handheld_entry.pad.r_stick = pad_state.r_stick;
            break;
        case NPadControllerType::JoyDual:
            dual_entry.connection_status.raw = 0;

            dual_entry.connection_status.IsLeftJoyConnected.Assign(1);
            dual_entry.connection_status.IsRightJoyConnected.Assign(1);
            dual_entry.connection_status.IsConnected.Assign(1);

            libnx_entry.connection_status.IsLeftJoyConnected.Assign(1);
            libnx_entry.connection_status.IsRightJoyConnected.Assign(1);
            libnx_entry.connection_status.IsConnected.Assign(1);

            dual_entry.pad.pad_states.raw = pad_state.pad_states.raw;
            dual_entry.pad.l_stick = pad_state.l_stick;
            dual_entry.pad.r_stick = pad_state.r_stick;
            break;
        case NPadControllerType::JoyLeft:
            left_entry.connection_status.raw = 0;

            left_entry.connection_status.IsConnected.Assign(1);
            left_entry.pad.pad_states.raw = pad_state.pad_states.raw;
            left_entry.pad.l_stick = pad_state.l_stick;
            left_entry.pad.r_stick = pad_state.r_stick;
            break;
        case NPadControllerType::JoyRight:
            right_entry.connection_status.raw = 0;

            right_entry.connection_status.IsConnected.Assign(1);
            right_entry.pad.pad_states.raw = pad_state.pad_states.raw;
            right_entry.pad.l_stick = pad_state.l_stick;
            right_entry.pad.r_stick = pad_state.r_stick;
            break;
        case NPadControllerType::Pokeball:
            pokeball_entry.connection_status.raw = 0;

            pokeball_entry.connection_status.IsConnected.Assign(1);
            pokeball_entry.connection_status.IsWired.Assign(1);

            pokeball_entry.pad.pad_states.raw = pad_state.pad_states.raw;
            pokeball_entry.pad.l_stick = pad_state.l_stick;
            pokeball_entry.pad.r_stick = pad_state.r_stick;
            break;
        case NPadControllerType::ProController:
            main_controller.connection_status.raw = 0;

            main_controller.connection_status.IsConnected.Assign(1);
            main_controller.connection_status.IsWired.Assign(1);
            main_controller.pad.pad_states.raw = pad_state.pad_states.raw;
            main_controller.pad.l_stick = pad_state.l_stick;
            main_controller.pad.r_stick = pad_state.r_stick;
            break;
        }

        // LibNX exclusively uses this section, so we always update it since LibNX doesn't activate
        // any controllers.
        libnx_entry.pad.pad_states.raw = pad_state.pad_states.raw;
        libnx_entry.pad.l_stick = pad_state.l_stick;
        libnx_entry.pad.r_stick = pad_state.r_stick;

        press_state |= static_cast<u32>(pad_state.pad_states.raw);
    }
    std::memcpy(data + NPAD_OFFSET, shared_memory_entries.data(),
                shared_memory_entries.size() * sizeof(NPadEntry));
}

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
    for (std::size_t i = 0; i < connected_controllers.size(); i++) {
        auto& controller = connected_controllers[i];
        if (!controller.is_connected) {
            continue;
        }
        const auto requested_controller =
            i <= MAX_NPAD_ID ? MapSettingsTypeToNPad(Settings::values.players[i].type)
                             : NPadControllerType::Handheld;
        if (!IsControllerSupported(requested_controller)) {
            const auto is_handheld = requested_controller == NPadControllerType::Handheld;
            if (is_handheld) {
                controller.type = NPadControllerType::None;
                controller.is_connected = false;
                AddNewController(requested_controller);
            } else {
                controller.type = requested_controller;
                InitNewlyAddedControler(i);
            }
        }
    }
}

void Controller_NPad::GetSupportedNpadIdTypes(u32* data, std::size_t max_length) {
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
    const std::size_t npad_index = NPadIdToIndex(npad_id);
    ASSERT(npad_index < shared_memory_entries.size());
    if (shared_memory_entries[npad_index].pad_assignment != assignment_mode) {
        shared_memory_entries[npad_index].pad_assignment = assignment_mode;
    }
}

void Controller_NPad::VibrateController(const std::vector<u32>& controller_ids,
                                        const std::vector<Vibration>& vibrations) {
    LOG_WARNING(Service_HID, "(STUBBED) called");

    if (!can_controllers_vibrate) {
        return;
    }
    for (std::size_t i = 0; i < controller_ids.size(); i++) {
        std::size_t controller_pos = NPadIdToIndex(static_cast<u32>(i));
        if (connected_controllers[controller_pos].is_connected) {
            // TODO(ogniK): Vibrate the physical controller
        }
    }
    last_processed_vibration = vibrations.back();
}

Kernel::SharedPtr<Kernel::ReadableEvent> Controller_NPad::GetStyleSetChangedEvent(
    u32 npad_id) const {
    // TODO(ogniK): Figure out the best time to signal this event. This event seems that it should
    // be signalled at least once, and signaled after a new controller is connected?
    const auto& styleset_event = styleset_changed_events[NPadIdToIndex(npad_id)];
    return styleset_event.readable;
}

Controller_NPad::Vibration Controller_NPad::GetLastVibration() const {
    return last_processed_vibration;
}

void Controller_NPad::AddNewController(NPadControllerType controller) {
    controller = DecideBestController(controller);
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

void Controller_NPad::AddNewControllerAt(NPadControllerType controller, u32 npad_id) {
    controller = DecideBestController(controller);
    if (controller == NPadControllerType::Handheld) {
        connected_controllers[NPadIdToIndex(NPAD_HANDHELD)] = {controller, true};
        InitNewlyAddedControler(NPadIdToIndex(NPAD_HANDHELD));
        return;
    }

    connected_controllers[NPadIdToIndex(npad_id)] = {controller, true};
    InitNewlyAddedControler(NPadIdToIndex(npad_id));
}

void Controller_NPad::ConnectNPad(u32 npad_id) {
    connected_controllers[NPadIdToIndex(npad_id)].is_connected = true;
}

void Controller_NPad::DisconnectNPad(u32 npad_id) {
    connected_controllers[NPadIdToIndex(npad_id)].is_connected = false;
}

void Controller_NPad::StartLRAssignmentMode() {
    // Nothing internally is used for lr assignment mode. Since we have the ability to set the
    // controller types from boot, it doesn't really matter about showing a selection screen
    is_in_lr_assignment_mode = true;
}

void Controller_NPad::StopLRAssignmentMode() {
    is_in_lr_assignment_mode = false;
}

bool Controller_NPad::SwapNpadAssignment(u32 npad_id_1, u32 npad_id_2) {
    if (npad_id_1 == NPAD_HANDHELD || npad_id_2 == NPAD_HANDHELD || npad_id_1 == NPAD_UNKNOWN ||
        npad_id_2 == NPAD_UNKNOWN) {
        return true;
    }
    const auto npad_index_1 = NPadIdToIndex(npad_id_1);
    const auto npad_index_2 = NPadIdToIndex(npad_id_2);

    if (!IsControllerSupported(connected_controllers[npad_index_1].type) ||
        !IsControllerSupported(connected_controllers[npad_index_2].type)) {
        return false;
    }

    std::swap(connected_controllers[npad_index_1].type, connected_controllers[npad_index_2].type);

    InitNewlyAddedControler(npad_index_1);
    InitNewlyAddedControler(npad_index_2);

    return true;
}

bool Controller_NPad::IsControllerSupported(NPadControllerType controller) {
    if (controller == NPadControllerType::Handheld) {
        // Handheld is not even a supported type, lets stop here
        if (std::find(supported_npad_id_types.begin(), supported_npad_id_types.end(),
                      NPAD_HANDHELD) == supported_npad_id_types.end()) {
            return false;
        }
        // Handheld should not be supported in docked mode
        if (Settings::values.use_docked_mode) {
            return false;
        }
    }
    switch (controller) {
    case NPadControllerType::ProController:
        return style.pro_controller;
    case NPadControllerType::Handheld:
        return style.handheld;
    case NPadControllerType::JoyDual:
        return style.joycon_dual;
    case NPadControllerType::JoyLeft:
        return style.joycon_left;
    case NPadControllerType::JoyRight:
        return style.joycon_right;
    case NPadControllerType::Pokeball:
        return style.pokeball;
    default:
        return false;
    }
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

bool Controller_NPad::IsVibrationEnabled() const {
    return can_controllers_vibrate;
}

void Controller_NPad::ClearAllConnectedControllers() {
    for (auto& controller : connected_controllers) {
        if (controller.is_connected && controller.type != NPadControllerType::None) {
            controller.type = NPadControllerType::None;
            controller.is_connected = false;
        }
    }
}

void Controller_NPad::DisconnectAllConnectedControllers() {
    std::for_each(connected_controllers.begin(), connected_controllers.end(),
                  [](ControllerHolder& controller) { controller.is_connected = false; });
}

void Controller_NPad::ConnectAllDisconnectedControllers() {
    std::for_each(connected_controllers.begin(), connected_controllers.end(),
                  [](ControllerHolder& controller) {
                      if (controller.type != NPadControllerType::None && !controller.is_connected) {
                          controller.is_connected = false;
                      }
                  });
}

void Controller_NPad::ClearAllControllers() {
    std::for_each(connected_controllers.begin(), connected_controllers.end(),
                  [](ControllerHolder& controller) {
                      controller.type = NPadControllerType::None;
                      controller.is_connected = false;
                  });
}

u32 Controller_NPad::GetAndResetPressState() {
    return std::exchange(press_state, 0);
}

bool Controller_NPad::IsControllerSupported(NPadControllerType controller) const {
    const bool support_handheld =
        std::find(supported_npad_id_types.begin(), supported_npad_id_types.end(), NPAD_HANDHELD) !=
        supported_npad_id_types.end();
    if (controller == NPadControllerType::Handheld) {
        // Handheld is not even a supported type, lets stop here
        if (!support_handheld) {
            return false;
        }
        // Handheld should not be supported in docked mode
        if (Settings::values.use_docked_mode) {
            return false;
        }

        return true;
    }
    if (std::any_of(supported_npad_id_types.begin(), supported_npad_id_types.end(),
                    [](u32 npad_id) { return npad_id <= MAX_NPAD_ID; })) {
        switch (controller) {
        case NPadControllerType::ProController:
            return style.pro_controller;
        case NPadControllerType::JoyDual:
            return style.joycon_dual;
        case NPadControllerType::JoyLeft:
            return style.joycon_left;
        case NPadControllerType::JoyRight:
            return style.joycon_right;
        case NPadControllerType::Pokeball:
            return style.pokeball;
        default:
            return false;
        }
    }
    return false;
}

Controller_NPad::NPadControllerType Controller_NPad::DecideBestController(
    NPadControllerType priority) const {
    if (IsControllerSupported(priority)) {
        return priority;
    }
    const auto is_docked = Settings::values.use_docked_mode;
    if (is_docked && priority == NPadControllerType::Handheld) {
        priority = NPadControllerType::JoyDual;
        if (IsControllerSupported(priority)) {
            return priority;
        }
    }
    std::vector<NPadControllerType> priority_list;
    switch (priority) {
    case NPadControllerType::ProController:
        priority_list.push_back(NPadControllerType::JoyDual);
        if (!is_docked) {
            priority_list.push_back(NPadControllerType::Handheld);
        }
        priority_list.push_back(NPadControllerType::JoyLeft);
        priority_list.push_back(NPadControllerType::JoyRight);
        priority_list.push_back(NPadControllerType::Pokeball);
        break;
    case NPadControllerType::Handheld:
        priority_list.push_back(NPadControllerType::JoyDual);
        priority_list.push_back(NPadControllerType::ProController);
        priority_list.push_back(NPadControllerType::JoyLeft);
        priority_list.push_back(NPadControllerType::JoyRight);
        priority_list.push_back(NPadControllerType::Pokeball);
        break;
    case NPadControllerType::JoyDual:
        if (!is_docked) {
            priority_list.push_back(NPadControllerType::Handheld);
        }
        priority_list.push_back(NPadControllerType::ProController);
        priority_list.push_back(NPadControllerType::JoyLeft);
        priority_list.push_back(NPadControllerType::JoyRight);
        priority_list.push_back(NPadControllerType::Pokeball);
        break;
    case NPadControllerType::JoyLeft:
        priority_list.push_back(NPadControllerType::JoyRight);
        priority_list.push_back(NPadControllerType::JoyDual);
        if (!is_docked) {
            priority_list.push_back(NPadControllerType::Handheld);
        }
        priority_list.push_back(NPadControllerType::ProController);
        priority_list.push_back(NPadControllerType::Pokeball);
        break;
    case NPadControllerType::JoyRight:
        priority_list.push_back(NPadControllerType::JoyLeft);
        priority_list.push_back(NPadControllerType::JoyDual);
        if (!is_docked) {
            priority_list.push_back(NPadControllerType::Handheld);
        }
        priority_list.push_back(NPadControllerType::ProController);
        priority_list.push_back(NPadControllerType::Pokeball);
        break;
    case NPadControllerType::Pokeball:
        priority_list.push_back(NPadControllerType::JoyLeft);
        priority_list.push_back(NPadControllerType::JoyRight);
        priority_list.push_back(NPadControllerType::JoyDual);
        if (!is_docked) {
            priority_list.push_back(NPadControllerType::Handheld);
        }
        priority_list.push_back(NPadControllerType::ProController);
        break;
    default:
        priority_list.push_back(NPadControllerType::JoyDual);
        if (!is_docked) {
            priority_list.push_back(NPadControllerType::Handheld);
        }
        priority_list.push_back(NPadControllerType::ProController);
        priority_list.push_back(NPadControllerType::JoyLeft);
        priority_list.push_back(NPadControllerType::JoyRight);
        priority_list.push_back(NPadControllerType::JoyDual);
    }

    const auto iter = std::find_if(priority_list.begin(), priority_list.end(),
                                   [this](auto type) { return IsControllerSupported(type); });
    if (iter == priority_list.end()) {
        UNIMPLEMENTED_MSG("Could not find supported controller!");
        return priority;
    }

    return *iter;
}

} // namespace Service::HID
