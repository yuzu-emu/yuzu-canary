// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include "common/common_types.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/core_timing_util.h"
#include "core/frontend/emu_window.h"
#include "core/frontend/input.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/client_port.h"
#include "core/hle/kernel/client_session.h"
#include "core/hle/kernel/readable_event.h"
#include "core/hle/kernel/shared_memory.h"
#include "core/hle/kernel/writable_event.h"
#include "core/hle/service/hid/errors.h"
#include "core/hle/service/hid/hid.h"
#include "core/hle/service/hid/irs.h"
#include "core/hle/service/hid/xcd.h"
#include "core/hle/service/service.h"
#include "core/settings.h"

#include "core/hle/service/hid/controllers/controller_base.h"
#include "core/hle/service/hid/controllers/debug_pad.h"
#include "core/hle/service/hid/controllers/gesture.h"
#include "core/hle/service/hid/controllers/keyboard.h"
#include "core/hle/service/hid/controllers/mouse.h"
#include "core/hle/service/hid/controllers/npad.h"
#include "core/hle/service/hid/controllers/stubbed.h"
#include "core/hle/service/hid/controllers/touchscreen.h"
#include "core/hle/service/hid/controllers/xpad.h"

namespace Service::HID {

// Updating period for each HID device.
// TODO(ogniK): Find actual polling rate of hid
constexpr s64 pad_update_ticks = static_cast<s64>(Core::Timing::BASE_CLOCK_RATE / 66);
constexpr s64 accelerometer_update_ticks = static_cast<s64>(Core::Timing::BASE_CLOCK_RATE / 100);
constexpr s64 gyroscope_update_ticks = static_cast<s64>(Core::Timing::BASE_CLOCK_RATE / 100);
constexpr std::size_t SHARED_MEMORY_SIZE = 0x40000;

IAppletResource::IAppletResource(Core::System& system)
    : ServiceFramework("IAppletResource"), system(system) {
    static const FunctionInfo functions[] = {
        {0, &IAppletResource::GetSharedMemoryHandle, "GetSharedMemoryHandle"},
    };
    RegisterHandlers(functions);

    auto& kernel = system.Kernel();
    shared_mem = Kernel::SharedMemory::Create(
        kernel, nullptr, SHARED_MEMORY_SIZE, Kernel::MemoryPermission::ReadWrite,
        Kernel::MemoryPermission::Read, 0, Kernel::MemoryRegion::BASE, "HID:SharedMemory");

    MakeController<Controller_DebugPad>(HidController::DebugPad);
    MakeController<Controller_Touchscreen>(HidController::Touchscreen);
    MakeController<Controller_Mouse>(HidController::Mouse);
    MakeController<Controller_Keyboard>(HidController::Keyboard);
    MakeController<Controller_XPad>(HidController::XPad);
    MakeController<Controller_Stubbed>(HidController::Unknown1);
    MakeController<Controller_Stubbed>(HidController::Unknown2);
    MakeController<Controller_Stubbed>(HidController::Unknown3);
    MakeController<Controller_Stubbed>(HidController::SixAxisSensor);
    MakeController<Controller_NPad>(HidController::NPad);
    MakeController<Controller_Gesture>(HidController::Gesture);

    // Homebrew doesn't try to activate some controllers, so we activate them by default
    GetController<Controller_NPad>(HidController::NPad).ActivateController();
    GetController<Controller_Touchscreen>(HidController::Touchscreen).ActivateController();

    GetController<Controller_Stubbed>(HidController::Unknown1).SetCommonHeaderOffset(0x4c00);
    GetController<Controller_Stubbed>(HidController::Unknown2).SetCommonHeaderOffset(0x4e00);
    GetController<Controller_Stubbed>(HidController::Unknown3).SetCommonHeaderOffset(0x5000);

    // Register update callbacks
    auto& core_timing = system.CoreTiming();
    pad_update_event =
        core_timing.RegisterEvent("HID::UpdatePadCallback", [this](u64 userdata, s64 cycles_late) {
            UpdateControllers(userdata, cycles_late);
        });

    // TODO(shinyquagsire23): Other update callbacks? (accel, gyro?)

    core_timing.ScheduleEvent(pad_update_ticks, pad_update_event);

    ReloadInputDevices();
}

void IAppletResource::ActivateController(HidController controller) {
    controllers[static_cast<size_t>(controller)]->ActivateController();
}

void IAppletResource::DeactivateController(HidController controller) {
    controllers[static_cast<size_t>(controller)]->DeactivateController();
}

IAppletResource ::~IAppletResource() {
    system.CoreTiming().UnscheduleEvent(pad_update_event, 0);
}

void IAppletResource::GetSharedMemoryHandle(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_HID, "called");

    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushCopyObjects(shared_mem);
}

void IAppletResource::UpdateControllers(u64 userdata, s64 cycles_late) {
    auto& core_timing = system.CoreTiming();

    const bool should_reload = Settings::values.is_device_reload_pending.exchange(false);
    for (const auto& controller : controllers) {
        if (should_reload) {
            controller->OnLoadInputDevices();
        }
        controller->OnUpdate(core_timing, shared_mem->GetPointer(), SHARED_MEMORY_SIZE);
    }

    core_timing.ScheduleEvent(pad_update_ticks - cycles_late, pad_update_event);
}

class IActiveVibrationDeviceList final : public ServiceFramework<IActiveVibrationDeviceList> {
public:
    IActiveVibrationDeviceList() : ServiceFramework("IActiveVibrationDeviceList") {
        static const FunctionInfo functions[] = {
            {0, &IActiveVibrationDeviceList::ActivateVibrationDevice, "ActivateVibrationDevice"},
        };
        RegisterHandlers(functions);
    }

private:
    void ActivateVibrationDevice(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_HID, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }
};

std::shared_ptr<IAppletResource> Hid::GetAppletResource() {
    if (applet_resource == nullptr) {
        applet_resource = std::make_shared<IAppletResource>(system);
    }

    return applet_resource;
}

Hid::Hid(Core::System& system) : ServiceFramework("hid"), system(system) {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &Hid::CreateAppletResource, "CreateAppletResource"},
        {1, &Hid::ActivateDebugPad, "ActivateDebugPad"},
        {11, &Hid::ActivateTouchScreen, "ActivateTouchScreen"},
        {21, &Hid::ActivateMouse, "ActivateMouse"},
        {31, &Hid::ActivateKeyboard, "ActivateKeyboard"},
        {32, nullptr, "SendKeyboardLockKeyEvent"},
        {40, nullptr, "AcquireXpadIdEventHandle"},
        {41, nullptr, "ReleaseXpadIdEventHandle"},
        {51, &Hid::ActivateXpad, "ActivateXpad"},
        {55, nullptr, "GetXpadIds"},
        {56, nullptr, "ActivateJoyXpad"},
        {58, nullptr, "GetJoyXpadLifoHandle"},
        {59, nullptr, "GetJoyXpadIds"},
        {60, nullptr, "ActivateSixAxisSensor"},
        {61, nullptr, "DeactivateSixAxisSensor"},
        {62, nullptr, "GetSixAxisSensorLifoHandle"},
        {63, nullptr, "ActivateJoySixAxisSensor"},
        {64, nullptr, "DeactivateJoySixAxisSensor"},
        {65, nullptr, "GetJoySixAxisSensorLifoHandle"},
        {66, &Hid::StartSixAxisSensor, "StartSixAxisSensor"},
        {67, &Hid::StopSixAxisSensor, "StopSixAxisSensor"},
        {68, nullptr, "IsSixAxisSensorFusionEnabled"},
        {69, nullptr, "EnableSixAxisSensorFusion"},
        {70, nullptr, "SetSixAxisSensorFusionParameters"},
        {71, nullptr, "GetSixAxisSensorFusionParameters"},
        {72, nullptr, "ResetSixAxisSensorFusionParameters"},
        {73, nullptr, "SetAccelerometerParameters"},
        {74, nullptr, "GetAccelerometerParameters"},
        {75, nullptr, "ResetAccelerometerParameters"},
        {76, nullptr, "SetAccelerometerPlayMode"},
        {77, nullptr, "GetAccelerometerPlayMode"},
        {78, nullptr, "ResetAccelerometerPlayMode"},
        {79, &Hid::SetGyroscopeZeroDriftMode, "SetGyroscopeZeroDriftMode"},
        {80, nullptr, "GetGyroscopeZeroDriftMode"},
        {81, nullptr, "ResetGyroscopeZeroDriftMode"},
        {82, &Hid::IsSixAxisSensorAtRest, "IsSixAxisSensorAtRest"},
        {83, nullptr, "IsFirmwareUpdateAvailableForSixAxisSensor"},
        {91, &Hid::ActivateGesture, "ActivateGesture"},
        {100, &Hid::SetSupportedNpadStyleSet, "SetSupportedNpadStyleSet"},
        {101, &Hid::GetSupportedNpadStyleSet, "GetSupportedNpadStyleSet"},
        {102, &Hid::SetSupportedNpadIdType, "SetSupportedNpadIdType"},
        {103, &Hid::ActivateNpad, "ActivateNpad"},
        {104, nullptr, "DeactivateNpad"},
        {106, &Hid::AcquireNpadStyleSetUpdateEventHandle, "AcquireNpadStyleSetUpdateEventHandle"},
        {107, &Hid::DisconnectNpad, "DisconnectNpad"},
        {108, &Hid::GetPlayerLedPattern, "GetPlayerLedPattern"},
        {109, &Hid::ActivateNpadWithRevision, "ActivateNpadWithRevision"},
        {120, &Hid::SetNpadJoyHoldType, "SetNpadJoyHoldType"},
        {121, &Hid::GetNpadJoyHoldType, "GetNpadJoyHoldType"},
        {122, &Hid::SetNpadJoyAssignmentModeSingleByDefault, "SetNpadJoyAssignmentModeSingleByDefault"},
        {123, &Hid::SetNpadJoyAssignmentModeSingle, "SetNpadJoyAssignmentModeSingle"},
        {124, &Hid::SetNpadJoyAssignmentModeDual, "SetNpadJoyAssignmentModeDual"},
        {125, &Hid::MergeSingleJoyAsDualJoy, "MergeSingleJoyAsDualJoy"},
        {126, &Hid::StartLrAssignmentMode, "StartLrAssignmentMode"},
        {127, &Hid::StopLrAssignmentMode, "StopLrAssignmentMode"},
        {128, &Hid::SetNpadHandheldActivationMode, "SetNpadHandheldActivationMode"},
        {129, &Hid::GetNpadHandheldActivationMode, "GetNpadHandheldActivationMode"},
        {130, &Hid::SwapNpadAssignment, "SwapNpadAssignment"},
        {131, nullptr, "IsUnintendedHomeButtonInputProtectionEnabled"},
        {132, nullptr, "EnableUnintendedHomeButtonInputProtection"},
        {133, nullptr, "SetNpadJoyAssignmentModeSingleWithDestination"},
        {134, nullptr, "SetNpadAnalogStickUseCenterClamp"},
        {200, &Hid::GetVibrationDeviceInfo, "GetVibrationDeviceInfo"},
        {201, &Hid::SendVibrationValue, "SendVibrationValue"},
        {202, &Hid::GetActualVibrationValue, "GetActualVibrationValue"},
        {203, &Hid::CreateActiveVibrationDeviceList, "CreateActiveVibrationDeviceList"},
        {204, &Hid::PermitVibration, "PermitVibration"},
        {205, &Hid::IsVibrationPermitted, "IsVibrationPermitted"},
        {206, &Hid::SendVibrationValues, "SendVibrationValues"},
        {207, nullptr, "SendVibrationGcErmCommand"},
        {208, nullptr, "GetActualVibrationGcErmCommand"},
        {209, &Hid::BeginPermitVibrationSession, "BeginPermitVibrationSession"},
        {210, &Hid::EndPermitVibrationSession, "EndPermitVibrationSession"},
        {211, nullptr, "IsVibrationDeviceMounted"},
        {300, &Hid::ActivateConsoleSixAxisSensor, "ActivateConsoleSixAxisSensor"},
        {301, &Hid::StartConsoleSixAxisSensor, "StartConsoleSixAxisSensor"},
        {302, nullptr, "StopConsoleSixAxisSensor"},
        {303, nullptr, "ActivateSevenSixAxisSensor"},
        {304, nullptr, "StartSevenSixAxisSensor"},
        {305, nullptr, "StopSevenSixAxisSensor"},
        {306, nullptr, "InitializeSevenSixAxisSensor"},
        {307, nullptr, "FinalizeSevenSixAxisSensor"},
        {308, nullptr, "SetSevenSixAxisSensorFusionStrength"},
        {309, nullptr, "GetSevenSixAxisSensorFusionStrength"},
        {310, nullptr, "ResetSevenSixAxisSensorTimestamp"},
        {400, nullptr, "IsUsbFullKeyControllerEnabled"},
        {401, nullptr, "EnableUsbFullKeyController"},
        {402, nullptr, "IsUsbFullKeyControllerConnected"},
        {403, nullptr, "HasBattery"},
        {404, nullptr, "HasLeftRightBattery"},
        {405, nullptr, "GetNpadInterfaceType"},
        {406, nullptr, "GetNpadLeftRightInterfaceType"},
        {500, nullptr, "GetPalmaConnectionHandle"},
        {501, nullptr, "InitializePalma"},
        {502, nullptr, "AcquirePalmaOperationCompleteEvent"},
        {503, nullptr, "GetPalmaOperationInfo"},
        {504, nullptr, "PlayPalmaActivity"},
        {505, nullptr, "SetPalmaFrModeType"},
        {506, nullptr, "ReadPalmaStep"},
        {507, nullptr, "EnablePalmaStep"},
        {508, nullptr, "ResetPalmaStep"},
        {509, nullptr, "ReadPalmaApplicationSection"},
        {510, nullptr, "WritePalmaApplicationSection"},
        {511, nullptr, "ReadPalmaUniqueCode"},
        {512, nullptr, "SetPalmaUniqueCodeInvalid"},
        {513, nullptr, "WritePalmaActivityEntry"},
        {514, nullptr, "WritePalmaRgbLedPatternEntry"},
        {515, nullptr, "WritePalmaWaveEntry"},
        {516, nullptr, "SetPalmaDataBaseIdentificationVersion"},
        {517, nullptr, "GetPalmaDataBaseIdentificationVersion"},
        {518, nullptr, "SuspendPalmaFeature"},
        {519, nullptr, "GetPalmaOperationResult"},
        {520, nullptr, "ReadPalmaPlayLog"},
        {521, nullptr, "ResetPalmaPlayLog"},
        {522, &Hid::SetIsPalmaAllConnectable, "SetIsPalmaAllConnectable"},
        {523, nullptr, "SetIsPalmaPairedConnectable"},
        {524, nullptr, "PairPalma"},
        {525, &Hid::SetPalmaBoostMode, "SetPalmaBoostMode"},
        {526, nullptr, "CancelWritePalmaWaveEntry"},
        {1000, nullptr, "SetNpadCommunicationMode"},
        {1001, nullptr, "GetNpadCommunicationMode"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

Hid::~Hid() = default;

void Hid::CreateAppletResource(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    if (applet_resource == nullptr) {
        applet_resource = std::make_shared<IAppletResource>(system);
    }

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<IAppletResource>(applet_resource);
}

void Hid::ActivateXpad(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto basic_xpad_id{rp.Pop<u32>()};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, basic_xpad_id={}, applet_resource_user_id={}", basic_xpad_id,
              applet_resource_user_id);

    applet_resource->ActivateController(HidController::XPad);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::ActivateDebugPad(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    applet_resource->ActivateController(HidController::DebugPad);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::ActivateTouchScreen(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    applet_resource->ActivateController(HidController::Touchscreen);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::ActivateMouse(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    applet_resource->ActivateController(HidController::Mouse);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::ActivateKeyboard(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    applet_resource->ActivateController(HidController::Keyboard);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::ActivateGesture(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto unknown{rp.Pop<u32>()};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, unknown={}, applet_resource_user_id={}", unknown,
              applet_resource_user_id);

    applet_resource->ActivateController(HidController::Gesture);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::ActivateNpadWithRevision(Kernel::HLERequestContext& ctx) {
    // Should have no effect with how our npad sets up the data
    IPC::RequestParser rp{ctx};
    const auto unknown{rp.Pop<u32>()};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, unknown={}, applet_resource_user_id={}", unknown,
              applet_resource_user_id);

    applet_resource->ActivateController(HidController::NPad);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::StartSixAxisSensor(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto handle{rp.Pop<u32>()};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_WARNING(Service_HID, "(STUBBED) called, handle={}, applet_resource_user_id={}", handle,
                applet_resource_user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::SetGyroscopeZeroDriftMode(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto handle{rp.Pop<u32>()};
    const auto drift_mode{rp.Pop<u32>()};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_WARNING(Service_HID,
                "(STUBBED) called, handle={}, drift_mode={}, applet_resource_user_id={}", handle,
                drift_mode, applet_resource_user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::IsSixAxisSensorAtRest(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto handle{rp.Pop<u32>()};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_WARNING(Service_HID, "(STUBBED) called, handle={}, applet_resource_user_id={}", handle,
                applet_resource_user_id);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    // TODO (Hexagon12): Properly implement reading gyroscope values from controllers.
    rb.Push(true);
}

void Hid::SetSupportedNpadStyleSet(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto supported_styleset{rp.Pop<u32>()};

    LOG_DEBUG(Service_HID, "called, supported_styleset={}", supported_styleset);

    applet_resource->GetController<Controller_NPad>(HidController::NPad)
        .SetSupportedStyleSet({supported_styleset});

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::GetSupportedNpadStyleSet(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    auto& controller = applet_resource->GetController<Controller_NPad>(HidController::NPad);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(controller.GetSupportedStyleSet().raw);
}

void Hid::SetSupportedNpadIdType(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    applet_resource->GetController<Controller_NPad>(HidController::NPad)
        .SetSupportedNPadIdTypes(ctx.ReadBuffer().data(), ctx.GetReadBufferSize());
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::ActivateNpad(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    applet_resource->ActivateController(HidController::NPad);
}

void Hid::AcquireNpadStyleSetUpdateEventHandle(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto npad_id{rp.Pop<u32>()};
    const auto applet_resource_user_id{rp.Pop<u64>()};
    const auto unknown{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, npad_id={}, applet_resource_user_id={}, unknown={}", npad_id,
              applet_resource_user_id, unknown);

    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushCopyObjects(applet_resource->GetController<Controller_NPad>(HidController::NPad)
                           .GetStyleSetChangedEvent(npad_id));
}

void Hid::DisconnectNpad(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto npad_id{rp.Pop<u32>()};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, npad_id={}, applet_resource_user_id={}", npad_id,
              applet_resource_user_id);

    applet_resource->GetController<Controller_NPad>(HidController::NPad).DisconnectNPad(npad_id);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::GetPlayerLedPattern(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto npad_id{rp.Pop<u32>()};

    LOG_DEBUG(Service_HID, "called, npad_id={}", npad_id);

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(RESULT_SUCCESS);
    rb.PushRaw<u64>(applet_resource->GetController<Controller_NPad>(HidController::NPad)
                        .GetLedPattern(npad_id)
                        .raw);
}

void Hid::SetNpadJoyHoldType(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};
    const auto hold_type{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}, hold_type={}",
              applet_resource_user_id, hold_type);

    auto& controller = applet_resource->GetController<Controller_NPad>(HidController::NPad);
    controller.SetHoldType(Controller_NPad::NpadHoldType{hold_type});

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::GetNpadJoyHoldType(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    const auto& controller = applet_resource->GetController<Controller_NPad>(HidController::NPad);
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u64>(static_cast<u64>(controller.GetHoldType()));
}

void Hid::SetNpadJoyAssignmentModeSingleByDefault(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto npad_id{rp.Pop<u32>()};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_WARNING(Service_HID, "(STUBBED) called, npad_id={}, applet_resource_user_id={}", npad_id,
                applet_resource_user_id);

    auto& controller = applet_resource->GetController<Controller_NPad>(HidController::NPad);
    controller.SetNpadMode(npad_id, Controller_NPad::NPadAssignments::Single);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::SetNpadJoyAssignmentModeSingle(Kernel::HLERequestContext& ctx) {
    // TODO: Check the differences between this and SetNpadJoyAssignmentModeSingleByDefault
    IPC::RequestParser rp{ctx};
    const auto npad_id{rp.Pop<u32>()};
    const auto applet_resource_user_id{rp.Pop<u64>()};
    const auto npad_joy_device_type{rp.Pop<u64>()};

    LOG_WARNING(Service_HID,
                "(STUBBED) called, npad_id={}, applet_resource_user_id={}, npad_joy_device_type={}",
                npad_id, applet_resource_user_id, npad_joy_device_type);

    auto& controller = applet_resource->GetController<Controller_NPad>(HidController::NPad);
    controller.SetNpadMode(npad_id, Controller_NPad::NPadAssignments::Single);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::SetNpadJoyAssignmentModeDual(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto npad_id{rp.Pop<u32>()};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, npad_id={}, applet_resource_user_id={}", npad_id,
              applet_resource_user_id);

    auto& controller = applet_resource->GetController<Controller_NPad>(HidController::NPad);
    controller.SetNpadMode(npad_id, Controller_NPad::NPadAssignments::Dual);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::MergeSingleJoyAsDualJoy(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto unknown_1{rp.Pop<u32>()};
    const auto unknown_2{rp.Pop<u32>()};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_WARNING(Service_HID,
                "(STUBBED) called, unknown_1={}, unknown_2={}, applet_resource_user_id={}",
                unknown_1, unknown_2, applet_resource_user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::StartLrAssignmentMode(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);
    auto& controller = applet_resource->GetController<Controller_NPad>(HidController::NPad);
    controller.StartLRAssignmentMode();

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::StopLrAssignmentMode(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);
    auto& controller = applet_resource->GetController<Controller_NPad>(HidController::NPad);
    controller.StopLRAssignmentMode();

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::SetNpadHandheldActivationMode(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};
    const auto mode{rp.Pop<u64>()};

    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}, mode={}",
                applet_resource_user_id, mode);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::GetNpadHandheldActivationMode(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}",
                applet_resource_user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::SwapNpadAssignment(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto npad_1{rp.Pop<u32>()};
    const auto npad_2{rp.Pop<u32>()};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}, npad_1={}, npad_2={}",
              applet_resource_user_id, npad_1, npad_2);

    auto& controller = applet_resource->GetController<Controller_NPad>(HidController::NPad);
    IPC::ResponseBuilder rb{ctx, 2};
    if (controller.SwapNpadAssignment(npad_1, npad_2)) {
        rb.Push(RESULT_SUCCESS);
    } else {
        LOG_ERROR(Service_HID, "Npads are not connected!");
        rb.Push(ERR_NPAD_NOT_CONNECTED);
    }
}

void Hid::BeginPermitVibrationSession(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    applet_resource->GetController<Controller_NPad>(HidController::NPad).SetVibrationEnabled(true);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::EndPermitVibrationSession(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_HID, "called");

    applet_resource->GetController<Controller_NPad>(HidController::NPad).SetVibrationEnabled(false);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::SendVibrationValue(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto controller_id{rp.Pop<u32>()};
    const auto vibration_values{rp.PopRaw<Controller_NPad::Vibration>()};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, controller_id={}, applet_resource_user_id={}", controller_id,
              applet_resource_user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);

    applet_resource->GetController<Controller_NPad>(HidController::NPad)
        .VibrateController({controller_id}, {vibration_values});
}

void Hid::SendVibrationValues(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    const auto controllers = ctx.ReadBuffer(0);
    const auto vibrations = ctx.ReadBuffer(1);

    std::vector<u32> controller_list(controllers.size() / sizeof(u32));
    std::vector<Controller_NPad::Vibration> vibration_list(vibrations.size() /
                                                           sizeof(Controller_NPad::Vibration));

    std::memcpy(controller_list.data(), controllers.data(), controllers.size());
    std::memcpy(vibration_list.data(), vibrations.data(), vibrations.size());
    std::transform(controller_list.begin(), controller_list.end(), controller_list.begin(),
                   [](u32 controller_id) { return controller_id - 3; });

    applet_resource->GetController<Controller_NPad>(HidController::NPad)
        .VibrateController(controller_list, vibration_list);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::GetActualVibrationValue(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto controller_id{rp.Pop<u32>()};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, controller_id={}, applet_resource_user_id={}", controller_id,
              applet_resource_user_id);

    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(RESULT_SUCCESS);
    rb.PushRaw<Controller_NPad::Vibration>(
        applet_resource->GetController<Controller_NPad>(HidController::NPad).GetLastVibration());
}

void Hid::GetVibrationDeviceInfo(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_HID, "called");

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(1);
    rb.Push<u32>(0);
}

void Hid::CreateActiveVibrationDeviceList(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_HID, "called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<IActiveVibrationDeviceList>();
}

void Hid::PermitVibration(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto can_vibrate{rp.Pop<bool>()};
    applet_resource->GetController<Controller_NPad>(HidController::NPad)
        .SetVibrationEnabled(can_vibrate);

    LOG_DEBUG(Service_HID, "called, can_vibrate={}", can_vibrate);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::IsVibrationPermitted(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_HID, "called");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push(
        applet_resource->GetController<Controller_NPad>(HidController::NPad).IsVibrationEnabled());
}

void Hid::ActivateConsoleSixAxisSensor(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}",
                applet_resource_user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::StartConsoleSixAxisSensor(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto handle{rp.Pop<u32>()};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_WARNING(Service_HID, "(STUBBED) called, handle={}, applet_resource_user_id={}", handle,
                applet_resource_user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::StopSixAxisSensor(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto handle{rp.Pop<u32>()};

    LOG_WARNING(Service_HID, "(STUBBED) called, handle={}", handle);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::SetIsPalmaAllConnectable(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};
    const auto unknown{rp.Pop<u32>()};

    LOG_WARNING(Service_HID, "(STUBBED) called, applet_resource_user_id={}, unknown={}",
                applet_resource_user_id, unknown);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Hid::SetPalmaBoostMode(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto unknown{rp.Pop<u32>()};

    LOG_WARNING(Service_HID, "(STUBBED) called, unknown={}", unknown);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

class HidDbg final : public ServiceFramework<HidDbg> {
public:
    explicit HidDbg() : ServiceFramework{"hid:dbg"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "DeactivateDebugPad"},
            {1, nullptr, "SetDebugPadAutoPilotState"},
            {2, nullptr, "UnsetDebugPadAutoPilotState"},
            {10, nullptr, "DeactivateTouchScreen"},
            {11, nullptr, "SetTouchScreenAutoPilotState"},
            {12, nullptr, "UnsetTouchScreenAutoPilotState"},
            {20, nullptr, "DeactivateMouse"},
            {21, nullptr, "SetMouseAutoPilotState"},
            {22, nullptr, "UnsetMouseAutoPilotState"},
            {30, nullptr, "DeactivateKeyboard"},
            {31, nullptr, "SetKeyboardAutoPilotState"},
            {32, nullptr, "UnsetKeyboardAutoPilotState"},
            {50, nullptr, "DeactivateXpad"},
            {51, nullptr, "SetXpadAutoPilotState"},
            {52, nullptr, "UnsetXpadAutoPilotState"},
            {60, nullptr, "DeactivateJoyXpad"},
            {91, nullptr, "DeactivateGesture"},
            {110, nullptr, "DeactivateHomeButton"},
            {111, nullptr, "SetHomeButtonAutoPilotState"},
            {112, nullptr, "UnsetHomeButtonAutoPilotState"},
            {120, nullptr, "DeactivateSleepButton"},
            {121, nullptr, "SetSleepButtonAutoPilotState"},
            {122, nullptr, "UnsetSleepButtonAutoPilotState"},
            {123, nullptr, "DeactivateInputDetector"},
            {130, nullptr, "DeactivateCaptureButton"},
            {131, nullptr, "SetCaptureButtonAutoPilotState"},
            {132, nullptr, "UnsetCaptureButtonAutoPilotState"},
            {133, nullptr, "SetShiftAccelerometerCalibrationValue"},
            {134, nullptr, "GetShiftAccelerometerCalibrationValue"},
            {135, nullptr, "SetShiftGyroscopeCalibrationValue"},
            {136, nullptr, "GetShiftGyroscopeCalibrationValue"},
            {140, nullptr, "DeactivateConsoleSixAxisSensor"},
            {141, nullptr, "GetConsoleSixAxisSensorSamplingFrequency"},
            {142, nullptr, "DeactivateSevenSixAxisSensor"},
            {143, nullptr, "GetConsoleSixAxisSensorCountStates"},
            {201, nullptr, "ActivateFirmwareUpdate"},
            {202, nullptr, "DeactivateFirmwareUpdate"},
            {203, nullptr, "StartFirmwareUpdate"},
            {204, nullptr, "GetFirmwareUpdateStage"},
            {205, nullptr, "GetFirmwareVersion"},
            {206, nullptr, "GetDestinationFirmwareVersion"},
            {207, nullptr, "DiscardFirmwareInfoCacheForRevert"},
            {208, nullptr, "StartFirmwareUpdateForRevert"},
            {209, nullptr, "GetAvailableFirmwareVersionForRevert"},
            {210, nullptr, "IsFirmwareUpdatingDevice"},
            {211, nullptr, "StartFirmwareUpdateIndividual"},
            {215, nullptr, "SetUsbFirmwareForceUpdateEnabled"},
            {216, nullptr, "SetAllKuinaDevicesToFirmwareUpdateMode"},
            {221, nullptr, "UpdateControllerColor"},
            {222, nullptr, "ConnectUsbPadsAsync"},
            {223, nullptr, "DisconnectUsbPadsAsync"},
            {224, nullptr, "UpdateDesignInfo"},
            {225, nullptr, "GetUniquePadDriverState"},
            {226, nullptr, "GetSixAxisSensorDriverStates"},
            {227, nullptr, "GetRxPacketHistory"},
            {228, nullptr, "AcquireOperationEventHandle"},
            {229, nullptr, "ReadSerialFlash"},
            {230, nullptr, "WriteSerialFlash"},
            {231, nullptr, "GetOperationResult"},
            {232, nullptr, "EnableShipmentMode"},
            {233, nullptr, "ClearPairingInfo"},
            {234, nullptr, "GetUniquePadDeviceTypeSetInternal"},
            {235, nullptr, "EnableAnalogStickPower"},
            {301, nullptr, "GetAbstractedPadHandles"},
            {302, nullptr, "GetAbstractedPadState"},
            {303, nullptr, "GetAbstractedPadsState"},
            {321, nullptr, "SetAutoPilotVirtualPadState"},
            {322, nullptr, "UnsetAutoPilotVirtualPadState"},
            {323, nullptr, "UnsetAllAutoPilotVirtualPadState"},
            {324, nullptr, "AttachHdlsWorkBuffer"},
            {325, nullptr, "ReleaseHdlsWorkBuffer"},
            {326, nullptr, "DumpHdlsNpadAssignmentState"},
            {327, nullptr, "DumpHdlsStates"},
            {328, nullptr, "ApplyHdlsNpadAssignmentState"},
            {329, nullptr, "ApplyHdlsStateList"},
            {330, nullptr, "AttachHdlsVirtualDevice"},
            {331, nullptr, "DetachHdlsVirtualDevice"},
            {332, nullptr, "SetHdlsState"},
            {350, nullptr, "AddRegisteredDevice"},
            {400, nullptr, "DisableExternalMcuOnNxDevice"},
            {401, nullptr, "DisableRailDeviceFiltering"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class HidSys final : public ServiceFramework<HidSys> {
public:
    explicit HidSys() : ServiceFramework{"hid:sys"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {31, nullptr, "SendKeyboardLockKeyEvent"},
            {101, nullptr, "AcquireHomeButtonEventHandle"},
            {111, nullptr, "ActivateHomeButton"},
            {121, nullptr, "AcquireSleepButtonEventHandle"},
            {131, nullptr, "ActivateSleepButton"},
            {141, nullptr, "AcquireCaptureButtonEventHandle"},
            {151, nullptr, "ActivateCaptureButton"},
            {161, nullptr, "GetPlatformConfig"},
            {210, nullptr, "AcquireNfcDeviceUpdateEventHandle"},
            {211, nullptr, "GetNpadsWithNfc"},
            {212, nullptr, "AcquireNfcActivateEventHandle"},
            {213, nullptr, "ActivateNfc"},
            {214, nullptr, "GetXcdHandleForNpadWithNfc"},
            {215, nullptr, "IsNfcActivated"},
            {230, nullptr, "AcquireIrSensorEventHandle"},
            {231, nullptr, "ActivateIrSensor"},
            {301, nullptr, "ActivateNpadSystem"},
            {303, nullptr, "ApplyNpadSystemCommonPolicy"},
            {304, nullptr, "EnableAssigningSingleOnSlSrPress"},
            {305, nullptr, "DisableAssigningSingleOnSlSrPress"},
            {306, nullptr, "GetLastActiveNpad"},
            {307, nullptr, "GetNpadSystemExtStyle"},
            {308, nullptr, "ApplyNpadSystemCommonPolicyFull"},
            {309, nullptr, "GetNpadFullKeyGripColor"},
            {310, nullptr, "GetMaskedSupportedNpadStyleSet"},
            {311, nullptr, "SetNpadPlayerLedBlinkingDevice"},
            {312, nullptr, "SetSupportedNpadStyleSetAll"},
            {321, nullptr, "GetUniquePadsFromNpad"},
            {322, nullptr, "GetIrSensorState"},
            {323, nullptr, "GetXcdHandleForNpadWithIrSensor"},
            {500, nullptr, "SetAppletResourceUserId"},
            {501, nullptr, "RegisterAppletResourceUserId"},
            {502, nullptr, "UnregisterAppletResourceUserId"},
            {503, nullptr, "EnableAppletToGetInput"},
            {504, nullptr, "SetAruidValidForVibration"},
            {505, nullptr, "EnableAppletToGetSixAxisSensor"},
            {510, nullptr, "SetVibrationMasterVolume"},
            {511, nullptr, "GetVibrationMasterVolume"},
            {512, nullptr, "BeginPermitVibrationSession"},
            {513, nullptr, "EndPermitVibrationSession"},
            {520, nullptr, "EnableHandheldHids"},
            {521, nullptr, "DisableHandheldHids"},
            {540, nullptr, "AcquirePlayReportControllerUsageUpdateEvent"},
            {541, nullptr, "GetPlayReportControllerUsages"},
            {542, nullptr, "AcquirePlayReportRegisteredDeviceUpdateEvent"},
            {543, nullptr, "GetRegisteredDevicesOld"},
            {544, nullptr, "AcquireConnectionTriggerTimeoutEvent"},
            {545, nullptr, "SendConnectionTrigger"},
            {546, nullptr, "AcquireDeviceRegisteredEventForControllerSupport"},
            {547, nullptr, "GetAllowedBluetoothLinksCount"},
            {548, nullptr, "GetRegisteredDevices"},
            {549, nullptr, "GetConnectableRegisteredDevices"},
            {700, nullptr, "ActivateUniquePad"},
            {702, nullptr, "AcquireUniquePadConnectionEventHandle"},
            {703, nullptr, "GetUniquePadIds"},
            {751, nullptr, "AcquireJoyDetachOnBluetoothOffEventHandle"},
            {800, nullptr, "ListSixAxisSensorHandles"},
            {801, nullptr, "IsSixAxisSensorUserCalibrationSupported"},
            {802, nullptr, "ResetSixAxisSensorCalibrationValues"},
            {803, nullptr, "StartSixAxisSensorUserCalibration"},
            {804, nullptr, "CancelSixAxisSensorUserCalibration"},
            {805, nullptr, "GetUniquePadBluetoothAddress"},
            {806, nullptr, "DisconnectUniquePad"},
            {807, nullptr, "GetUniquePadType"},
            {808, nullptr, "GetUniquePadInterface"},
            {809, nullptr, "GetUniquePadSerialNumber"},
            {810, nullptr, "GetUniquePadControllerNumber"},
            {811, nullptr, "GetSixAxisSensorUserCalibrationStage"},
            {821, nullptr, "StartAnalogStickManualCalibration"},
            {822, nullptr, "RetryCurrentAnalogStickManualCalibrationStage"},
            {823, nullptr, "CancelAnalogStickManualCalibration"},
            {824, nullptr, "ResetAnalogStickManualCalibration"},
            {825, nullptr, "GetAnalogStickState"},
            {826, nullptr, "GetAnalogStickManualCalibrationStage"},
            {827, nullptr, "IsAnalogStickButtonPressed"},
            {828, nullptr, "IsAnalogStickInReleasePosition"},
            {829, nullptr, "IsAnalogStickInCircumference"},
            {830, nullptr, "SetNotificationLedPattern"},
            {850, nullptr, "IsUsbFullKeyControllerEnabled"},
            {851, nullptr, "EnableUsbFullKeyController"},
            {852, nullptr, "IsUsbConnected"},
            {870, nullptr, "IsHandheldButtonPressedOnConsoleMode"},
            {900, nullptr, "ActivateInputDetector"},
            {901, nullptr, "NotifyInputDetector"},
            {1000, nullptr, "InitializeFirmwareUpdate"},
            {1001, nullptr, "GetFirmwareVersion"},
            {1002, nullptr, "GetAvailableFirmwareVersion"},
            {1003, nullptr, "IsFirmwareUpdateAvailable"},
            {1004, nullptr, "CheckFirmwareUpdateRequired"},
            {1005, nullptr, "StartFirmwareUpdate"},
            {1006, nullptr, "AbortFirmwareUpdate"},
            {1007, nullptr, "GetFirmwareUpdateState"},
            {1008, nullptr, "ActivateAudioControl"},
            {1009, nullptr, "AcquireAudioControlEventHandle"},
            {1010, nullptr, "GetAudioControlStates"},
            {1011, nullptr, "DeactivateAudioControl"},
            {1050, nullptr, "IsSixAxisSensorAccurateUserCalibrationSupported"},
            {1051, nullptr, "StartSixAxisSensorAccurateUserCalibration"},
            {1052, nullptr, "CancelSixAxisSensorAccurateUserCalibration"},
            {1053, nullptr, "GetSixAxisSensorAccurateUserCalibrationState"},
            {1100, nullptr, "GetHidbusSystemServiceObject"},
            {1120, nullptr, "SetFirmwareHotfixUpdateSkipEnabled"},
            {1130, nullptr, "InitializeUsbFirmwareUpdate"},
            {1131, nullptr, "FinalizeUsbFirmwareUpdate"},
            {1132, nullptr, "CheckUsbFirmwareUpdateRequired"},
            {1133, nullptr, "StartUsbFirmwareUpdate"},
            {1134, nullptr, "GetUsbFirmwareUpdateState"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class HidTmp final : public ServiceFramework<HidTmp> {
public:
    explicit HidTmp() : ServiceFramework{"hid:tmp"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "GetConsoleSixAxisSensorCalibrationValues"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class HidBus final : public ServiceFramework<HidBus> {
public:
    explicit HidBus() : ServiceFramework{"hidbus"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {1, nullptr, "GetBusHandle"},
            {2, nullptr, "IsExternalDeviceConnected"},
            {3, nullptr, "Initialize"},
            {4, nullptr, "Finalize"},
            {5, nullptr, "EnableExternalDevice"},
            {6, nullptr, "GetExternalDeviceId"},
            {7, nullptr, "SendCommandAsync"},
            {8, nullptr, "GetSendCommandAsynceResult"},
            {9, nullptr, "SetEventForSendCommandAsycResult"},
            {10, nullptr, "GetSharedMemoryHandle"},
            {11, nullptr, "EnableJoyPollingReceiveMode"},
            {12, nullptr, "DisableJoyPollingReceiveMode"},
            {13, nullptr, "GetPollingData"},
            {14, nullptr, "SetStatusManagerType"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

void ReloadInputDevices() {
    Settings::values.is_device_reload_pending.store(true);
}

void InstallInterfaces(SM::ServiceManager& service_manager, Core::System& system) {
    std::make_shared<Hid>(system)->InstallAsService(service_manager);
    std::make_shared<HidBus>()->InstallAsService(service_manager);
    std::make_shared<HidDbg>()->InstallAsService(service_manager);
    std::make_shared<HidSys>()->InstallAsService(service_manager);
    std::make_shared<HidTmp>()->InstallAsService(service_manager);

    std::make_shared<IRS>(system)->InstallAsService(service_manager);
    std::make_shared<IRS_SYS>()->InstallAsService(service_manager);

    std::make_shared<XCD_SYS>()->InstallAsService(service_manager);
}

} // namespace Service::HID
