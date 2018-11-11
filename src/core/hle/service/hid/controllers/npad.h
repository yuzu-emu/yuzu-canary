// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include "common/bit_field.h"
#include "common/common_types.h"
#include "core/frontend/input.h"
#include "core/hle/kernel/event.h"
#include "core/hle/service/hid/controllers/controller_base.h"
#include "core/settings.h"

namespace Service::HID {

constexpr u32 NPAD_HANDHELD = 32;
constexpr u32 NPAD_UNKNOWN = 16; // TODO(ogniK): What is this?

class Controller_NPad final : public ControllerBase {
public:
    Controller_NPad();
    ~Controller_NPad() override;

    // Called when the controller is initialized
    void OnInit() override;

    // When the controller is released
    void OnRelease() override;

    // When the controller is requesting an update for the shared memory
    void OnUpdate(u8* data, std::size_t size) override;

    // Called when input devices should be loaded
    void OnLoadInputDevices() override;

    struct NPadType {
        union {
            u32_le raw{};

            BitField<0, 1, u32_le> pro_controller;
            BitField<1, 1, u32_le> handheld;
            BitField<2, 1, u32_le> joycon_dual;
            BitField<3, 1, u32_le> joycon_left;
            BitField<4, 1, u32_le> joycon_right;

            BitField<6, 1, u32_le> pokeball; // TODO(ogniK): Confirm when possible
        };
    };
    static_assert(sizeof(NPadType) == 4, "NPadType is an invalid size");

    struct Vibration {
        f32 amp_low;
        f32 freq_low;
        f32 amp_high;
        f32 freq_high;
    };
    static_assert(sizeof(Vibration) == 0x10, "Vibration is an invalid size");

    enum class NpadHoldType : u64 {
        Vertical = 0,
        Horizontal = 1,
    };

    enum class NPadAssignments : u32_le {
        Dual = 0,
        Single = 1,
    };

    enum class NPadControllerType {
        None,
        ProController,
        Handheld,
        JoyDual,
        JoyLeft,
        JoyRight,
        Pokeball,
    };

    struct LedPattern {
        explicit LedPattern(u64 light1, u64 light2, u64 light3, u64 light4) {
            position1.Assign(light1);
            position1.Assign(light2);
            position1.Assign(light3);
            position1.Assign(light4);
        }
        union {
            u64 raw{};
            BitField<0, 1, u64> position1;
            BitField<1, 1, u64> position2;
            BitField<2, 1, u64> position3;
            BitField<3, 1, u64> position4;
        };
    };

    void SetSupportedStyleSet(NPadType style_set);
    NPadType GetSupportedStyleSet() const;

    void SetSupportedNPadIdTypes(u8* data, std::size_t length);
    void GetSupportedNpadIdTypes(u32* data, std::size_t max_length);
    std::size_t GetSupportedNPadIdTypesSize() const;

    void SetHoldType(NpadHoldType joy_hold_type);
    NpadHoldType GetHoldType() const;

    void SetNpadMode(u32 npad_id, NPadAssignments assignment_mode);

    void VibrateController(const std::vector<u32>& controller_ids,
                           const std::vector<Vibration>& vibrations);

    Kernel::SharedPtr<Kernel::Event> GetStyleSetChangedEvent() const;
    Vibration GetLastVibration() const;

    void AddNewController(NPadControllerType controller);
    void AddNewControllerAt(NPadControllerType controller, u32 npad_id);

    void ConnectNPad(u32 npad_id);
    void DisconnectNPad(u32 npad_id);
    LedPattern GetLedPattern(u32 npad_id);
    void SetVibrationEnabled(bool can_vibrate);
    void ClearAllConnectedControllers();
    void DisconnectAllConnectedControllers();
    void ConnectAllDisconnectedControllers();
    void ClearAllControllers();

    static std::size_t NPadIdToIndex(u32 npad_id);
    static u32 IndexToNPad(std::size_t index);

private:
    struct CommonHeader {
        s64_le timestamp;
        s64_le total_entry_count;
        s64_le last_entry_index;
        s64_le entry_count;
    };
    static_assert(sizeof(CommonHeader) == 0x20, "CommonHeader is an invalid size");

    struct ControllerColor {
        u32_le body_color;
        u32_le button_color;
    };
    static_assert(sizeof(ControllerColor) == 8, "ControllerColor is an invalid size");

    struct ControllerPadState {
        union {
            u64_le raw{};
            // Button states
            BitField<0, 1, u64_le> a;
            BitField<1, 1, u64_le> b;
            BitField<2, 1, u64_le> x;
            BitField<3, 1, u64_le> y;
            BitField<4, 1, u64_le> l_stick;
            BitField<5, 1, u64_le> r_stick;
            BitField<6, 1, u64_le> l;
            BitField<7, 1, u64_le> r;
            BitField<8, 1, u64_le> zl;
            BitField<9, 1, u64_le> zr;
            BitField<10, 1, u64_le> plus;
            BitField<11, 1, u64_le> minus;

            // D-Pad
            BitField<12, 1, u64_le> d_left;
            BitField<13, 1, u64_le> d_up;
            BitField<14, 1, u64_le> d_right;
            BitField<15, 1, u64_le> d_down;

            // Left JoyStick
            BitField<16, 1, u64_le> l_stick_left;
            BitField<17, 1, u64_le> l_stick_up;
            BitField<18, 1, u64_le> l_stick_right;
            BitField<19, 1, u64_le> l_stick_down;

            // Right JoyStick
            BitField<20, 1, u64_le> r_stick_left;
            BitField<21, 1, u64_le> r_stick_up;
            BitField<22, 1, u64_le> r_stick_right;
            BitField<23, 1, u64_le> r_stick_down;

            // Not always active?
            BitField<24, 1, u64_le> left_sl;
            BitField<25, 1, u64_le> left_sr;

            BitField<26, 1, u64_le> right_sl;
            BitField<27, 1, u64_le> right_sr;
        };
    };
    static_assert(sizeof(ControllerPadState) == 8, "ControllerPadState is an invalid size");

    struct AnalogPosition {
        s32_le x;
        s32_le y;
    };
    static_assert(sizeof(AnalogPosition) == 8, "AnalogPosition is an invalid size");

    struct ConnectionState {
        union {
            u32_le raw{};
            BitField<0, 1, u32_le> IsConnected;
            BitField<1, 1, u32_le> IsWired;
            BitField<2, 1, u32_le> IsLeftJoyConnected;
            BitField<3, 1, u32_le> IsLeftJoyWired;
            BitField<4, 1, u32_le> IsRightJoyConnected;
            BitField<5, 1, u32_le> IsRightJoyWired;
        };
    };
    static_assert(sizeof(ConnectionState) == 4, "ConnectionState is an invalid size");

    struct ControllerPad {
        ControllerPadState pad_states;
        AnalogPosition l_stick;
        AnalogPosition r_stick;
    };
    static_assert(sizeof(ControllerPad) == 0x18, "ControllerPad is an invalid size");

    struct GenericStates {
        s64_le timestamp;
        s64_le timestamp2;
        ControllerPad pad;
        ConnectionState connection_status;
    };
    static_assert(sizeof(GenericStates) == 0x30, "NPadGenericStates is an invalid size");

    struct NPadGeneric {
        CommonHeader common;
        std::array<GenericStates, 17> npad;
    };
    static_assert(sizeof(NPadGeneric) == 0x350, "NPadGeneric is an invalid size");

    enum class ColorReadError : u32_le {
        ReadOk = 0,
        ColorDoesntExist = 1,
        NoController = 2,
    };

    struct NPadProperties {
        union {
            s64_le raw{};
            BitField<11, 1, s64_le> is_vertical;
            BitField<12, 1, s64_le> is_horizontal;
            BitField<13, 1, s64_le> use_plus;
            BitField<14, 1, s64_le> use_minus;
        };
    };

    struct NPadDevice {
        union {
            u32_le raw{};
            BitField<0, 1, s32_le> pro_controller;
            BitField<1, 1, s32_le> handheld;
            BitField<2, 1, s32_le> handheld_left;
            BitField<3, 1, s32_le> handheld_right;
            BitField<4, 1, s32_le> joycon_left;
            BitField<5, 1, s32_le> joycon_right;
            BitField<6, 1, s32_le> pokeball;
        };
    };

    struct NPadEntry {
        NPadType joy_styles;
        NPadAssignments pad_assignment;

        ColorReadError single_color_error;
        ControllerColor single_color;

        ColorReadError dual_color_error;
        ControllerColor left_color;
        ControllerColor right_color;

        NPadGeneric main_controller_states;
        NPadGeneric handheld_states;
        NPadGeneric dual_states;
        NPadGeneric left_joy_states;
        NPadGeneric right_joy_states;
        NPadGeneric pokeball_states;
        NPadGeneric libnx; // TODO(ogniK): Find out what this actually is, libnx seems to only be
                           // relying on this for the time being
        INSERT_PADDING_BYTES(
            0x708 *
            6); // TODO(ogniK): SixAxis states, require more information before implementation
        NPadDevice device_type;
        NPadProperties properties;
        INSERT_PADDING_WORDS(1);
        std::array<u32, 3> battery_level;
        INSERT_PADDING_BYTES(0x5c);
        INSERT_PADDING_BYTES(0xdf8);
    };
    static_assert(sizeof(NPadEntry) == 0x5000, "NPadEntry is an invalid size");

    struct ControllerHolder {
        NPadControllerType type;
        bool is_connected;
    };

    NPadType style{};
    std::array<NPadEntry, 10> shared_memory_entries{};
    std::array<
        std::array<std::unique_ptr<Input::ButtonDevice>, Settings::NativeButton::NUM_BUTTONS_HID>,
        10>
        buttons;
    std::array<
        std::array<std::unique_ptr<Input::AnalogDevice>, Settings::NativeAnalog::NUM_STICKS_HID>,
        10>
        sticks;
    std::vector<u32> supported_npad_id_types{};
    NpadHoldType hold_type{NpadHoldType::Vertical};
    Kernel::SharedPtr<Kernel::Event> styleset_changed_event;
    Vibration last_processed_vibration{};
    std::array<ControllerHolder, 10> connected_controllers{};
    bool can_controllers_vibrate{true};

    void InitNewlyAddedControler(std::size_t controller_idx);
    bool IsControllerSupported(NPadControllerType controller) const;
    NPadControllerType DecideBestController(NPadControllerType priority) const;
    void RequestPadStateUpdate(u32 npad_id);
    std::array<ControllerPad, 10> npad_pad_states{};
    bool IsControllerSupported(NPadControllerType controller);
};
} // namespace Service::HID
