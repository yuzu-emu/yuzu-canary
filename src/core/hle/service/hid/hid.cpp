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
#include "core/hle/kernel/event.h"
#include "core/hle/kernel/shared_memory.h"
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
// TODO(shinyquagsire23): These need better values.
constexpr u64 pad_update_ticks = CoreTiming::BASE_CLOCK_RATE / 100;
constexpr u64 accelerometer_update_ticks = CoreTiming::BASE_CLOCK_RATE / 100;
constexpr u64 gyroscope_update_ticks = CoreTiming::BASE_CLOCK_RATE / 100;
constexpr std::size_t SHARED_MEMORY_SIZE = 0x40000;
enum class HidController : std::size_t {
    DebugPad,
    Touchscreen,
    Mouse,
    Keyboard,
    XPad,
    Unknown1,
    Unknown2,
    Unknown3,
    SixAxisSensor,
    NPad,
    Gesture,

    MaxControllers,
};

class IAppletResource final : public ServiceFramework<IAppletResource> {
public:
    IAppletResource() : ServiceFramework("IAppletResource") {
        static const FunctionInfo functions[] = {
            {0, &IAppletResource::GetSharedMemoryHandle, "GetSharedMemoryHandle"},
        };
        RegisterHandlers(functions);

        auto& kernel = Core::System::GetInstance().Kernel();
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
        pad_update_event = CoreTiming::RegisterEvent(
            "HID::UpdatePadCallback",
            [this](u64 userdata, int cycles_late) { UpdateControllers(userdata, cycles_late); });

        // TODO(shinyquagsire23): Other update callbacks? (accel, gyro?)

        CoreTiming::ScheduleEvent(pad_update_ticks, pad_update_event);
    }

    void ActivateController(HidController controller) {
        controllers[static_cast<size_t>(controller)]->ActivateController();
    }

    void DeactivateController(HidController controller) {
        controllers[static_cast<size_t>(controller)]->DeactivateController();
    }

    template <typename T>
    void MakeController(HidController controller) {
        controllers[static_cast<std::size_t>(controller)] = std::make_unique<T>();
    }

    template <typename T>
    T& GetController(HidController controller) {
        return static_cast<T&>(*controllers[static_cast<size_t>(controller)]);
    }

    ~IAppletResource() {
        CoreTiming::UnscheduleEvent(pad_update_event, 0);
    }

private:
    void GetSharedMemoryHandle(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushCopyObjects(shared_mem);
        LOG_DEBUG(Service_HID, "called");
    }

    void UpdateControllers(u64 userdata, int cycles_late) {
        const bool should_reload = Settings::values.is_device_reload_pending.exchange(false);
        for (const auto& controller : controllers) {
            if (should_reload) {
                controller->OnLoadInputDevices();
            }
            controller->OnUpdate(shared_mem->GetPointer(), SHARED_MEMORY_SIZE);
        }

        CoreTiming::ScheduleEvent(pad_update_ticks - cycles_late, pad_update_event);
    }

    // Handle to shared memory region designated to HID service
    Kernel::SharedPtr<Kernel::SharedMemory> shared_mem;

    // CoreTiming update events
    CoreTiming::EventType* pad_update_event;

    std::array<std::unique_ptr<ControllerBase>, static_cast<size_t>(HidController::MaxControllers)>
        controllers{};
};

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
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        LOG_WARNING(Service_HID, "(STUBBED) called");
    }
};

class Hid final : public ServiceFramework<Hid> {
public:
    Hid() : ServiceFramework("hid") {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &Hid::CreateAppletResource, "CreateAppletResource"},
            {1, &Hid::ActivateDebugPad, "ActivateDebugPad"},
            {11, &Hid::ActivateTouchScreen, "ActivateTouchScreen"},
            {21, &Hid::ActivateMouse, "ActivateMouse"},
            {31, &Hid::ActivateKeyboard, "ActivateKeyboard"},
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
            {123, nullptr, "SetNpadJoyAssignmentModeSingleByDefault"},
            {124, &Hid::SetNpadJoyAssignmentModeDual, "SetNpadJoyAssignmentModeDual"},
            {125, &Hid::MergeSingleJoyAsDualJoy, "MergeSingleJoyAsDualJoy"},
            {126, nullptr, "StartLrAssignmentMode"},
            {127, nullptr, "StopLrAssignmentMode"},
            {128, &Hid::SetNpadHandheldActivationMode, "SetNpadHandheldActivationMode"},
            {129, nullptr, "GetNpadHandheldActivationMode"},
            {130, nullptr, "SwapNpadAssignment"},
            {131, nullptr, "IsUnintendedHomeButtonInputProtectionEnabled"},
            {132, nullptr, "EnableUnintendedHomeButtonInputProtection"},
            {133, nullptr, "SetNpadJoyAssignmentModeSingleWithDestination"},
            {200, &Hid::GetVibrationDeviceInfo, "GetVibrationDeviceInfo"},
            {201, &Hid::SendVibrationValue, "SendVibrationValue"},
            {202, &Hid::GetActualVibrationValue, "GetActualVibrationValue"},
            {203, &Hid::CreateActiveVibrationDeviceList, "CreateActiveVibrationDeviceList"},
            {204, nullptr, "PermitVibration"},
            {205, nullptr, "IsVibrationPermitted"},
            {206, &Hid::SendVibrationValues, "SendVibrationValues"},
            {207, nullptr, "SendVibrationGcErmCommand"},
            {208, nullptr, "GetActualVibrationGcErmCommand"},
            {209, nullptr, "BeginPermitVibrationSession"},
            {210, nullptr, "EndPermitVibrationSession"},
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
            {508, nullptr, "SuspendPalmaStep"},
            {509, nullptr, "ResetPalmaStep"},
            {510, nullptr, "ReadPalmaApplicationSection"},
            {511, nullptr, "WritePalmaApplicationSection"},
            {512, nullptr, "ReadPalmaUniqueCode"},
            {513, nullptr, "SetPalmaUniqueCodeInvalid"},
            {1000, nullptr, "SetNpadCommunicationMode"},
            {1001, nullptr, "GetNpadCommunicationMode"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
    ~Hid() = default;

private:
    std::shared_ptr<IAppletResource> applet_resource;

    void CreateAppletResource(Kernel::HLERequestContext& ctx) {
        if (applet_resource == nullptr) {
            applet_resource = std::make_shared<IAppletResource>();
        }

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<IAppletResource>(applet_resource);
        LOG_DEBUG(Service_HID, "called");
    }

    void ActivateXpad(Kernel::HLERequestContext& ctx) {
        applet_resource->ActivateController(HidController::XPad);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        LOG_DEBUG(Service_HID, "called");
    }

    void ActivateDebugPad(Kernel::HLERequestContext& ctx) {
        applet_resource->ActivateController(HidController::DebugPad);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        LOG_DEBUG(Service_HID, "called");
    }

    void ActivateTouchScreen(Kernel::HLERequestContext& ctx) {
        applet_resource->ActivateController(HidController::Touchscreen);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        LOG_DEBUG(Service_HID, "called");
    }

    void ActivateMouse(Kernel::HLERequestContext& ctx) {
        applet_resource->ActivateController(HidController::Mouse);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        LOG_DEBUG(Service_HID, "called");
    }

    void ActivateKeyboard(Kernel::HLERequestContext& ctx) {
        applet_resource->ActivateController(HidController::Keyboard);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        LOG_DEBUG(Service_HID, "called");
    }

    void ActivateGesture(Kernel::HLERequestContext& ctx) {
        applet_resource->ActivateController(HidController::Gesture);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        LOG_DEBUG(Service_HID, "called");
    }

    void ActivateNpadWithRevision(Kernel::HLERequestContext& ctx) {
        // Should have no effect with how our npad sets up the data
        applet_resource->ActivateController(HidController::NPad);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        LOG_DEBUG(Service_HID, "called");
    }

    void StartSixAxisSensor(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        LOG_WARNING(Service_HID, "(STUBBED) called");
    }

    void SetGyroscopeZeroDriftMode(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        LOG_WARNING(Service_HID, "(STUBBED) called");
    }

    void IsSixAxisSensorAtRest(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        // TODO (Hexagon12): Properly implement reading gyroscope values from controllers.
        rb.Push(true);
        LOG_WARNING(Service_HID, "(STUBBED) called");
    }

    void SetSupportedNpadStyleSet(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        auto supported_styleset = rp.PopRaw<u32>();
        applet_resource->GetController<Controller_NPad>(HidController::NPad)
            .SetSupportedStyleSet({supported_styleset});

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);

        LOG_DEBUG(Service_HID, "called");
    }

    void GetSupportedNpadStyleSet(Kernel::HLERequestContext& ctx) {
        auto& controller = applet_resource->GetController<Controller_NPad>(HidController::NPad);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(controller.GetSupportedStyleSet().raw);
        LOG_DEBUG(Service_HID, "called");
    }

    void SetSupportedNpadIdType(Kernel::HLERequestContext& ctx) {
        applet_resource->GetController<Controller_NPad>(HidController::NPad)
            .SetSupportedNPadIdTypes(ctx.ReadBuffer().data(), ctx.GetReadBufferSize());
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        LOG_DEBUG(Service_HID, "called");
    }

    void ActivateNpad(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        applet_resource->ActivateController(HidController::NPad);
        LOG_DEBUG(Service_HID, "called");
    }

    void AcquireNpadStyleSetUpdateEventHandle(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushCopyObjects(applet_resource->GetController<Controller_NPad>(HidController::NPad)
                               .GetStyleSetChangedEvent());
        LOG_DEBUG(Service_HID, "called");
    }

    void DisconnectNpad(Kernel::HLERequestContext& ctx) {
        applet_resource->DeactivateController(HidController::NPad);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        LOG_DEBUG(Service_HID, "called");
    }

    void GetPlayerLedPattern(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        LOG_WARNING(Service_HID, "(STUBBED) called");
    }

    void SetNpadJoyHoldType(Kernel::HLERequestContext& ctx) {
        auto& controller = applet_resource->GetController<Controller_NPad>(HidController::NPad);
        IPC::RequestParser rp{ctx};
        const auto hold_type = rp.PopRaw<u64>();
        controller.SetHoldType(Controller_NPad::NpadHoldType{hold_type});

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        LOG_DEBUG(Service_HID, "called");
    }

    void GetNpadJoyHoldType(Kernel::HLERequestContext& ctx) {
        const auto& controller =
            applet_resource->GetController<Controller_NPad>(HidController::NPad);
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u64>(static_cast<u64>(controller.GetHoldType()));
        LOG_DEBUG(Service_HID, "called");
    }

    void SetNpadJoyAssignmentModeSingleByDefault(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        LOG_WARNING(Service_HID, "(STUBBED) called");
    }

    void SendVibrationValue(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto controller_id = rp.PopRaw<u32>();
        const auto vibration_values = rp.PopRaw<Controller_NPad::Vibration>();

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);

        applet_resource->GetController<Controller_NPad>(HidController::NPad)
            .VibrateController({controller_id}, {vibration_values});
        LOG_DEBUG(Service_HID, "called");
    }

    void SendVibrationValues(Kernel::HLERequestContext& ctx) {
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
        LOG_DEBUG(Service_HID, "called");
    }

    void GetActualVibrationValue(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 6};
        rb.Push(RESULT_SUCCESS);
        rb.PushRaw<Controller_NPad::Vibration>(
            applet_resource->GetController<Controller_NPad>(HidController::NPad)
                .GetLastVibration());
        LOG_DEBUG(Service_HID, "called");
    }

    void SetNpadJoyAssignmentModeDual(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto npad_id = rp.PopRaw<u32>();
        auto& controller = applet_resource->GetController<Controller_NPad>(HidController::NPad);
        controller.SetNpadMode(npad_id, Controller_NPad::NPadAssignments::Dual);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        LOG_DEBUG(Service_HID, "called");
    }

    void MergeSingleJoyAsDualJoy(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        LOG_WARNING(Service_HID, "(STUBBED) called");
    }

    void SetNpadHandheldActivationMode(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        LOG_WARNING(Service_HID, "(STUBBED) called");
    }

    void GetVibrationDeviceInfo(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(1);
        rb.Push<u32>(0);
        LOG_DEBUG(Service_HID, "called");
    }

    void CreateActiveVibrationDeviceList(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<IActiveVibrationDeviceList>();
        LOG_DEBUG(Service_HID, "called");
    }

    void ActivateConsoleSixAxisSensor(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        LOG_WARNING(Service_HID, "(STUBBED) called");
    }

    void StartConsoleSixAxisSensor(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        LOG_WARNING(Service_HID, "(STUBBED) called");
    }

    void StopSixAxisSensor(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        LOG_WARNING(Service_HID, "(STUBBED) called");
    }
};

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
            {221, nullptr, "UpdateControllerColor"},
            {222, nullptr, "ConnectUsbPadsAsync"},
            {223, nullptr, "DisconnectUsbPadsAsync"},
            {224, nullptr, "UpdateDesignInfo"},
            {225, nullptr, "GetUniquePadDriverState"},
            {226, nullptr, "GetSixAxisSensorDriverStates"},
            {301, nullptr, "GetAbstractedPadHandles"},
            {302, nullptr, "GetAbstractedPadState"},
            {303, nullptr, "GetAbstractedPadsState"},
            {321, nullptr, "SetAutoPilotVirtualPadState"},
            {322, nullptr, "UnsetAutoPilotVirtualPadState"},
            {323, nullptr, "UnsetAllAutoPilotVirtualPadState"},
            {350, nullptr, "AddRegisteredDevice"},
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
            {311, nullptr, "SetNpadPlayerLedBlinkingDevice"},
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
            {850, nullptr, "IsUsbFullKeyControllerEnabled"},
            {851, nullptr, "EnableUsbFullKeyController"},
            {852, nullptr, "IsUsbConnected"},
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
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

void ReloadInputDevices() {
    Settings::values.is_device_reload_pending.store(true);
}

void InstallInterfaces(SM::ServiceManager& service_manager) {
    std::make_shared<Hid>()->InstallAsService(service_manager);
    std::make_shared<HidBus>()->InstallAsService(service_manager);
    std::make_shared<HidDbg>()->InstallAsService(service_manager);
    std::make_shared<HidSys>()->InstallAsService(service_manager);
    std::make_shared<HidTmp>()->InstallAsService(service_manager);

    std::make_shared<IRS>()->InstallAsService(service_manager);
    std::make_shared<IRS_SYS>()->InstallAsService(service_manager);

    std::make_shared<XCD_SYS>()->InstallAsService(service_manager);
}

} // namespace Service::HID
