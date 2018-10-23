// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <cinttypes>
#include <cstring>
#include <stack>
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/event.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/am/applet_ae.h"
#include "core/hle/service/am/applet_oe.h"
#include "core/hle/service/am/idle.h"
#include "core/hle/service/am/omm.h"
#include "core/hle/service/am/spsm.h"
#include "core/hle/service/am/tcap.h"
#include "core/hle/service/apm/apm.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/nvflinger/nvflinger.h"
#include "core/hle/service/pm/pm.h"
#include "core/hle/service/set/set.h"
#include "core/hle/service/vi/vi.h"
#include "core/settings.h"

namespace Service::AM {

constexpr u32 POP_LAUNCH_PARAMETER_MAGIC = 0xC79497CA;

struct LaunchParameters {
    u32_le magic;
    u32_le is_account_selected;
    u128 current_user;
    INSERT_PADDING_BYTES(0x70);
};
static_assert(sizeof(LaunchParameters) == 0x88);

IWindowController::IWindowController() : ServiceFramework("IWindowController") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "CreateWindow"},
        {1, &IWindowController::GetAppletResourceUserId, "GetAppletResourceUserId"},
        {10, &IWindowController::AcquireForegroundRights, "AcquireForegroundRights"},
        {11, nullptr, "ReleaseForegroundRights"},
        {12, nullptr, "RejectToChangeIntoBackground"},
        {20, nullptr, "SetAppletWindowVisibility"},
        {21, nullptr, "SetAppletGpuTimeSlice"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IWindowController::~IWindowController() = default;

void IWindowController::GetAppletResourceUserId(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u64>(0);
}

void IWindowController::AcquireForegroundRights(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

IAudioController::IAudioController() : ServiceFramework("IAudioController") {
    static const FunctionInfo functions[] = {
        {0, &IAudioController::SetExpectedMasterVolume, "SetExpectedMasterVolume"},
        {1, &IAudioController::GetMainAppletExpectedMasterVolume,
         "GetMainAppletExpectedMasterVolume"},
        {2, &IAudioController::GetLibraryAppletExpectedMasterVolume,
         "GetLibraryAppletExpectedMasterVolume"},
        {3, nullptr, "ChangeMainAppletMasterVolume"},
        {4, nullptr, "SetTransparentVolumeRate"},
    };
    RegisterHandlers(functions);
}

IAudioController::~IAudioController() = default;

void IAudioController::SetExpectedMasterVolume(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void IAudioController::GetMainAppletExpectedMasterVolume(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push(volume);
}

void IAudioController::GetLibraryAppletExpectedMasterVolume(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push(volume);
}

IDisplayController::IDisplayController() : ServiceFramework("IDisplayController") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "GetLastForegroundCaptureImage"},
        {1, nullptr, "UpdateLastForegroundCaptureImage"},
        {2, nullptr, "GetLastApplicationCaptureImage"},
        {3, nullptr, "GetCallerAppletCaptureImage"},
        {4, nullptr, "UpdateCallerAppletCaptureImage"},
        {5, nullptr, "GetLastForegroundCaptureImageEx"},
        {6, nullptr, "GetLastApplicationCaptureImageEx"},
        {7, nullptr, "GetCallerAppletCaptureImageEx"},
        {8, nullptr, "TakeScreenShotOfOwnLayer"},  // 2.0.0+
        {9, nullptr, "CopyBetweenCaptureBuffers"}, // 5.0.0+
        {10, nullptr, "AcquireLastApplicationCaptureBuffer"},
        {11, nullptr, "ReleaseLastApplicationCaptureBuffer"},
        {12, nullptr, "AcquireLastForegroundCaptureBuffer"},
        {13, nullptr, "ReleaseLastForegroundCaptureBuffer"},
        {14, nullptr, "AcquireCallerAppletCaptureBuffer"},
        {15, nullptr, "ReleaseCallerAppletCaptureBuffer"},
        {16, nullptr, "AcquireLastApplicationCaptureBufferEx"},
        {17, nullptr, "AcquireLastForegroundCaptureBufferEx"},
        {18, nullptr, "AcquireCallerAppletCaptureBufferEx"},
        // 2.0.0+
        {20, nullptr, "ClearCaptureBuffer"},
        {21, nullptr, "ClearAppletTransitionBuffer"},
        // 4.0.0+
        {22, nullptr, "AcquireLastApplicationCaptureSharedBuffer"},
        {23, nullptr, "ReleaseLastApplicationCaptureSharedBuffer"},
        {24, nullptr, "AcquireLastForegroundCaptureSharedBuffer"},
        {25, nullptr, "ReleaseLastForegroundCaptureSharedBuffer"},
        {26, nullptr, "AcquireCallerAppletCaptureSharedBuffer"},
        {27, nullptr, "ReleaseCallerAppletCaptureSharedBuffer"},
        // 6.0.0+
        {28, nullptr, "TakeScreenShotOfOwnLayerEx"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IDisplayController::~IDisplayController() = default;

IDebugFunctions::IDebugFunctions() : ServiceFramework("IDebugFunctions") {}
IDebugFunctions::~IDebugFunctions() = default;

ISelfController::ISelfController(std::shared_ptr<NVFlinger::NVFlinger> nvflinger)
    : ServiceFramework("ISelfController"), nvflinger(std::move(nvflinger)) {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "Exit"},
        {1, &ISelfController::LockExit, "LockExit"},
        {2, &ISelfController::UnlockExit, "UnlockExit"},
        {3, nullptr, "EnterFatalSection"},
        {4, nullptr, "LeaveFatalSection"},
        {9, &ISelfController::GetLibraryAppletLaunchableEvent, "GetLibraryAppletLaunchableEvent"},
        {10, &ISelfController::SetScreenShotPermission, "SetScreenShotPermission"},
        {11, &ISelfController::SetOperationModeChangedNotification, "SetOperationModeChangedNotification"},
        {12, &ISelfController::SetPerformanceModeChangedNotification, "SetPerformanceModeChangedNotification"},
        {13, &ISelfController::SetFocusHandlingMode, "SetFocusHandlingMode"},
        {14, &ISelfController::SetRestartMessageEnabled, "SetRestartMessageEnabled"},
        {15, nullptr, "SetScreenShotAppletIdentityInfo"},
        {16, &ISelfController::SetOutOfFocusSuspendingEnabled, "SetOutOfFocusSuspendingEnabled"},
        {17, nullptr, "SetControllerFirmwareUpdateSection"},
        {18, nullptr, "SetRequiresCaptureButtonShortPressedMessage"},
        {19, &ISelfController::SetScreenShotImageOrientation, "SetScreenShotImageOrientation"},
        {20, nullptr, "SetDesirableKeyboardLayout"},
        {40, &ISelfController::CreateManagedDisplayLayer, "CreateManagedDisplayLayer"},
        {41, nullptr, "IsSystemBufferSharingEnabled"},
        {42, nullptr, "GetSystemSharedLayerHandle"},
        {50, &ISelfController::SetHandlesRequestToDisplay, "SetHandlesRequestToDisplay"},
        {51, nullptr, "ApproveToDisplay"},
        {60, nullptr, "OverrideAutoSleepTimeAndDimmingTime"},
        {61, nullptr, "SetMediaPlaybackState"},
        {62, &ISelfController::SetIdleTimeDetectionExtension, "SetIdleTimeDetectionExtension"},
        {63, &ISelfController::GetIdleTimeDetectionExtension, "GetIdleTimeDetectionExtension"},
        {64, nullptr, "SetInputDetectionSourceSet"},
        {65, nullptr, "ReportUserIsActive"},
        {66, nullptr, "GetCurrentIlluminance"},
        {67, nullptr, "IsIlluminanceAvailable"},
        {68, nullptr, "SetAutoSleepDisabled"},
        {69, nullptr, "IsAutoSleepDisabled"},
        {70, nullptr, "ReportMultimediaError"},
        {80, nullptr, "SetWirelessPriorityMode"},
        {90, nullptr, "GetAccumulatedSuspendedTickValue"},
        {91, nullptr, "GetAccumulatedSuspendedTickChangedEvent"},
        {1000, nullptr, "GetDebugStorageChannel"},
    };
    // clang-format on

    RegisterHandlers(functions);

    auto& kernel = Core::System::GetInstance().Kernel();
    launchable_event =
        Kernel::Event::Create(kernel, Kernel::ResetType::Sticky, "ISelfController:LaunchableEvent");
}

ISelfController::~ISelfController() = default;

void ISelfController::SetFocusHandlingMode(Kernel::HLERequestContext& ctx) {
    // Takes 3 input u8s with each field located immediately after the previous u8, these are
    // bool flags. No output.

    IPC::RequestParser rp{ctx};

    struct FocusHandlingModeParams {
        u8 unknown0;
        u8 unknown1;
        u8 unknown2;
    };
    auto flags = rp.PopRaw<FocusHandlingModeParams>();

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);

    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void ISelfController::SetRestartMessageEnabled(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);

    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void ISelfController::SetPerformanceModeChangedNotification(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};

    bool flag = rp.Pop<bool>();

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);

    LOG_WARNING(Service_AM, "(STUBBED) called flag={}", flag);
}

void ISelfController::SetScreenShotPermission(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);

    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void ISelfController::SetOperationModeChangedNotification(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};

    bool flag = rp.Pop<bool>();

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);

    LOG_WARNING(Service_AM, "(STUBBED) called flag={}", flag);
}

void ISelfController::SetOutOfFocusSuspendingEnabled(Kernel::HLERequestContext& ctx) {
    // Takes 3 input u8s with each field located immediately after the previous u8, these are
    // bool flags. No output.
    IPC::RequestParser rp{ctx};

    bool enabled = rp.Pop<bool>();

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);

    LOG_WARNING(Service_AM, "(STUBBED) called enabled={}", enabled);
}

void ISelfController::LockExit(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);

    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void ISelfController::UnlockExit(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);

    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void ISelfController::GetLibraryAppletLaunchableEvent(Kernel::HLERequestContext& ctx) {
    launchable_event->Signal();

    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushCopyObjects(launchable_event);

    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void ISelfController::SetScreenShotImageOrientation(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);

    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void ISelfController::CreateManagedDisplayLayer(Kernel::HLERequestContext& ctx) {
    // TODO(Subv): Find out how AM determines the display to use, for now just create the layer
    // in the Default display.
    u64 display_id = nvflinger->OpenDisplay("Default");
    u64 layer_id = nvflinger->CreateLayer(display_id);

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(RESULT_SUCCESS);
    rb.Push(layer_id);

    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void ISelfController::SetHandlesRequestToDisplay(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);

    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void ISelfController::SetIdleTimeDetectionExtension(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    idle_time_detection_extension = rp.Pop<u32>();
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);

    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void ISelfController::GetIdleTimeDetectionExtension(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(idle_time_detection_extension);

    LOG_WARNING(Service_AM, "(STUBBED) called");
}

ICommonStateGetter::ICommonStateGetter() : ServiceFramework("ICommonStateGetter") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &ICommonStateGetter::GetEventHandle, "GetEventHandle"},
        {1, &ICommonStateGetter::ReceiveMessage, "ReceiveMessage"},
        {2, nullptr, "GetThisAppletKind"},
        {3, nullptr, "AllowToEnterSleep"},
        {4, nullptr, "DisallowToEnterSleep"},
        {5, &ICommonStateGetter::GetOperationMode, "GetOperationMode"},
        {6, &ICommonStateGetter::GetPerformanceMode, "GetPerformanceMode"},
        {7, nullptr, "GetCradleStatus"},
        {8, &ICommonStateGetter::GetBootMode, "GetBootMode"},
        {9, &ICommonStateGetter::GetCurrentFocusState, "GetCurrentFocusState"},
        {10, nullptr, "RequestToAcquireSleepLock"},
        {11, nullptr, "ReleaseSleepLock"},
        {12, nullptr, "ReleaseSleepLockTransiently"},
        {13, nullptr, "GetAcquiredSleepLockEvent"},
        {20, nullptr, "PushToGeneralChannel"},
        {30, nullptr, "GetHomeButtonReaderLockAccessor"},
        {31, nullptr, "GetReaderLockAccessorEx"},
        {40, nullptr, "GetCradleFwVersion"},
        {50, nullptr, "IsVrModeEnabled"},
        {51, nullptr, "SetVrModeEnabled"},
        {52, nullptr, "SwitchLcdBacklight"},
        {55, nullptr, "IsInControllerFirmwareUpdateSection"},
        {60, &ICommonStateGetter::GetDefaultDisplayResolution, "GetDefaultDisplayResolution"},
        {61, &ICommonStateGetter::GetDefaultDisplayResolutionChangeEvent, "GetDefaultDisplayResolutionChangeEvent"},
        {62, nullptr, "GetHdcpAuthenticationState"},
        {63, nullptr, "GetHdcpAuthenticationStateChangeEvent"},
    };
    // clang-format on

    RegisterHandlers(functions);

    auto& kernel = Core::System::GetInstance().Kernel();
    event = Kernel::Event::Create(kernel, Kernel::ResetType::OneShot, "ICommonStateGetter:Event");
}

ICommonStateGetter::~ICommonStateGetter() = default;

void ICommonStateGetter::GetBootMode(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);

    rb.Push<u8>(static_cast<u8>(Service::PM::SystemBootMode::Normal)); // Normal boot mode

    LOG_DEBUG(Service_AM, "called");
}

void ICommonStateGetter::GetEventHandle(Kernel::HLERequestContext& ctx) {
    event->Signal();

    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushCopyObjects(event);

    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void ICommonStateGetter::ReceiveMessage(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(15);

    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void ICommonStateGetter::GetCurrentFocusState(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push(static_cast<u8>(FocusState::InFocus));

    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void ICommonStateGetter::GetDefaultDisplayResolutionChangeEvent(Kernel::HLERequestContext& ctx) {
    event->Signal();

    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushCopyObjects(event);

    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void ICommonStateGetter::GetDefaultDisplayResolution(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(RESULT_SUCCESS);

    if (Settings::values.use_docked_mode) {
        rb.Push(static_cast<u32>(Service::VI::DisplayResolution::DockedWidth));
        rb.Push(static_cast<u32>(Service::VI::DisplayResolution::DockedHeight));
    } else {
        rb.Push(static_cast<u32>(Service::VI::DisplayResolution::UndockedWidth));
        rb.Push(static_cast<u32>(Service::VI::DisplayResolution::UndockedHeight));
    }

    LOG_DEBUG(Service_AM, "called");
}

void ICommonStateGetter::GetOperationMode(Kernel::HLERequestContext& ctx) {
    const bool use_docked_mode{Settings::values.use_docked_mode};
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push(static_cast<u8>(use_docked_mode ? OperationMode::Docked : OperationMode::Handheld));

    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void ICommonStateGetter::GetPerformanceMode(Kernel::HLERequestContext& ctx) {
    const bool use_docked_mode{Settings::values.use_docked_mode};
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push(static_cast<u32>(use_docked_mode ? APM::PerformanceMode::Docked
                                             : APM::PerformanceMode::Handheld));

    LOG_WARNING(Service_AM, "(STUBBED) called");
}

class IStorageAccessor final : public ServiceFramework<IStorageAccessor> {
public:
    explicit IStorageAccessor(std::vector<u8> buffer)
        : ServiceFramework("IStorageAccessor"), buffer(std::move(buffer)) {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IStorageAccessor::GetSize, "GetSize"},
            {10, &IStorageAccessor::Write, "Write"},
            {11, &IStorageAccessor::Read, "Read"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    std::vector<u8> buffer;

    void GetSize(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 4};

        rb.Push(RESULT_SUCCESS);
        rb.Push(static_cast<u64>(buffer.size()));

        LOG_DEBUG(Service_AM, "called");
    }

    void Write(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};

        const u64 offset{rp.Pop<u64>()};
        const std::vector<u8> data{ctx.ReadBuffer()};

        ASSERT(offset + data.size() <= buffer.size());

        std::memcpy(&buffer[offset], data.data(), data.size());

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);

        LOG_DEBUG(Service_AM, "called, offset={}", offset);
    }

    void Read(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};

        const u64 offset{rp.Pop<u64>()};
        const std::size_t size{ctx.GetWriteBufferSize()};

        ASSERT(offset + size <= buffer.size());

        ctx.WriteBuffer(buffer.data() + offset, size);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);

        LOG_DEBUG(Service_AM, "called, offset={}", offset);
    }
};

class IStorage final : public ServiceFramework<IStorage> {
public:
    explicit IStorage(std::vector<u8> buffer)
        : ServiceFramework("IStorage"), buffer(std::move(buffer)) {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IStorage::Open, "Open"},
            {1, nullptr, "OpenTransferStorage"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    std::vector<u8> buffer;

    void Open(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};

        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<AM::IStorageAccessor>(buffer);

        LOG_DEBUG(Service_AM, "called");
    }
};

class ILibraryAppletAccessor final : public ServiceFramework<ILibraryAppletAccessor> {
public:
    explicit ILibraryAppletAccessor() : ServiceFramework("ILibraryAppletAccessor") {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &ILibraryAppletAccessor::GetAppletStateChangedEvent, "GetAppletStateChangedEvent"},
            {1, nullptr, "IsCompleted"},
            {10, &ILibraryAppletAccessor::Start, "Start"},
            {20, nullptr, "RequestExit"},
            {25, nullptr, "Terminate"},
            {30, &ILibraryAppletAccessor::GetResult, "GetResult"},
            {50, nullptr, "SetOutOfFocusApplicationSuspendingEnabled"},
            {100, &ILibraryAppletAccessor::PushInData, "PushInData"},
            {101, &ILibraryAppletAccessor::PopOutData, "PopOutData"},
            {102, nullptr, "PushExtraStorage"},
            {103, nullptr, "PushInteractiveInData"},
            {104, nullptr, "PopInteractiveOutData"},
            {105, nullptr, "GetPopOutDataEvent"},
            {106, nullptr, "GetPopInteractiveOutDataEvent"},
            {110, nullptr, "NeedsToExitProcess"},
            {120, nullptr, "GetLibraryAppletInfo"},
            {150, nullptr, "RequestForAppletToGetForeground"},
            {160, nullptr, "GetIndirectLayerConsumerHandle"},
        };
        // clang-format on

        RegisterHandlers(functions);

        auto& kernel = Core::System::GetInstance().Kernel();
        state_changed_event = Kernel::Event::Create(kernel, Kernel::ResetType::OneShot,
                                                    "ILibraryAppletAccessor:StateChangedEvent");
    }

private:
    void GetAppletStateChangedEvent(Kernel::HLERequestContext& ctx) {
        state_changed_event->Signal();

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushCopyObjects(state_changed_event);

        LOG_WARNING(Service_AM, "(STUBBED) called");
    }

    void GetResult(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);

        LOG_WARNING(Service_AM, "(STUBBED) called");
    }

    void Start(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);

        LOG_WARNING(Service_AM, "(STUBBED) called");
    }

    void PushInData(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        storage_stack.push(rp.PopIpcInterface<AM::IStorage>());

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);

        LOG_DEBUG(Service_AM, "called");
    }

    void PopOutData(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<AM::IStorage>(std::move(storage_stack.top()));

        storage_stack.pop();

        LOG_DEBUG(Service_AM, "called");
    }

    std::stack<std::shared_ptr<AM::IStorage>> storage_stack;
    Kernel::SharedPtr<Kernel::Event> state_changed_event;
};

ILibraryAppletCreator::ILibraryAppletCreator() : ServiceFramework("ILibraryAppletCreator") {
    static const FunctionInfo functions[] = {
        {0, &ILibraryAppletCreator::CreateLibraryApplet, "CreateLibraryApplet"},
        {1, nullptr, "TerminateAllLibraryApplets"},
        {2, nullptr, "AreAnyLibraryAppletsLeft"},
        {10, &ILibraryAppletCreator::CreateStorage, "CreateStorage"},
        {11, nullptr, "CreateTransferMemoryStorage"},
        {12, nullptr, "CreateHandleStorage"},
    };
    RegisterHandlers(functions);
}

ILibraryAppletCreator::~ILibraryAppletCreator() = default;

void ILibraryAppletCreator::CreateLibraryApplet(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};

    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<AM::ILibraryAppletAccessor>();

    LOG_DEBUG(Service_AM, "called");
}

void ILibraryAppletCreator::CreateStorage(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const u64 size{rp.Pop<u64>()};
    std::vector<u8> buffer(size);

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<AM::IStorage>(std::move(buffer));

    LOG_DEBUG(Service_AM, "called, size={}", size);
}

IApplicationFunctions::IApplicationFunctions() : ServiceFramework("IApplicationFunctions") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {1, &IApplicationFunctions::PopLaunchParameter, "PopLaunchParameter"},
        {10, nullptr, "CreateApplicationAndPushAndRequestToStart"},
        {11, nullptr, "CreateApplicationAndPushAndRequestToStartForQuest"},
        {12, nullptr, "CreateApplicationAndRequestToStart"},
        {13, &IApplicationFunctions::CreateApplicationAndRequestToStartForQuest, "CreateApplicationAndRequestToStartForQuest"},
        {20, &IApplicationFunctions::EnsureSaveData, "EnsureSaveData"},
        {21, &IApplicationFunctions::GetDesiredLanguage, "GetDesiredLanguage"},
        {22, &IApplicationFunctions::SetTerminateResult, "SetTerminateResult"},
        {23, &IApplicationFunctions::GetDisplayVersion, "GetDisplayVersion"},
        {24, nullptr, "GetLaunchStorageInfoForDebug"},
        {25, nullptr, "ExtendSaveData"},
        {26, nullptr, "GetSaveDataSize"},
        {30, &IApplicationFunctions::BeginBlockingHomeButtonShortAndLongPressed, "BeginBlockingHomeButtonShortAndLongPressed"},
        {31, &IApplicationFunctions::EndBlockingHomeButtonShortAndLongPressed, "EndBlockingHomeButtonShortAndLongPressed"},
        {32, &IApplicationFunctions::BeginBlockingHomeButton, "BeginBlockingHomeButton"},
        {33, &IApplicationFunctions::EndBlockingHomeButton, "EndBlockingHomeButton"},
        {40, &IApplicationFunctions::NotifyRunning, "NotifyRunning"},
        {50, &IApplicationFunctions::GetPseudoDeviceId, "GetPseudoDeviceId"},
        {60, nullptr, "SetMediaPlaybackStateForApplication"},
        {65, nullptr, "IsGamePlayRecordingSupported"},
        {66, &IApplicationFunctions::InitializeGamePlayRecording, "InitializeGamePlayRecording"},
        {67, &IApplicationFunctions::SetGamePlayRecordingState, "SetGamePlayRecordingState"},
        {68, nullptr, "RequestFlushGamePlayingMovieForDebug"},
        {70, nullptr, "RequestToShutdown"},
        {71, nullptr, "RequestToReboot"},
        {80, nullptr, "ExitAndRequestToShowThanksMessage"},
        {90, nullptr, "EnableApplicationCrashReport"},
        {100, nullptr, "InitializeApplicationCopyrightFrameBuffer"},
        {101, nullptr, "SetApplicationCopyrightImage"},
        {102, nullptr, "SetApplicationCopyrightVisibility"},
        {110, nullptr, "QueryApplicationPlayStatistics"},
        {120, nullptr, "ExecuteProgram"},
        {121, nullptr, "ClearUserChannel"},
        {122, nullptr, "UnpopToUserChannel"},
        {500, nullptr, "StartContinuousRecordingFlushForDebug"},
        {1000, nullptr, "CreateMovieMaker"},
        {1001, nullptr, "PrepareForJit"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IApplicationFunctions::~IApplicationFunctions() = default;

void IApplicationFunctions::BeginBlockingHomeButtonShortAndLongPressed(
    Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void IApplicationFunctions::EndBlockingHomeButtonShortAndLongPressed(
    Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void IApplicationFunctions::BeginBlockingHomeButton(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void IApplicationFunctions::EndBlockingHomeButton(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void IApplicationFunctions::PopLaunchParameter(Kernel::HLERequestContext& ctx) {
    LaunchParameters params{};

    params.magic = POP_LAUNCH_PARAMETER_MAGIC;
    params.is_account_selected = 1;

    Account::ProfileManager profile_manager{};
    const auto uuid = profile_manager.GetUser(Settings::values.current_user);
    ASSERT(uuid != boost::none);
    params.current_user = uuid->uuid;

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};

    rb.Push(RESULT_SUCCESS);

    std::vector<u8> buffer(sizeof(LaunchParameters));
    std::memcpy(buffer.data(), &params, buffer.size());

    rb.PushIpcInterface<AM::IStorage>(buffer);

    LOG_DEBUG(Service_AM, "called");
}

void IApplicationFunctions::CreateApplicationAndRequestToStartForQuest(
    Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void IApplicationFunctions::EnsureSaveData(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    u128 uid = rp.PopRaw<u128>(); // What does this do?

    LOG_WARNING(Service, "(STUBBED) called uid = {:016X}{:016X}", uid[1], uid[0]);

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u64>(0);
} // namespace Service::AM

void IApplicationFunctions::SetTerminateResult(Kernel::HLERequestContext& ctx) {
    // Takes an input u32 Result, no output.
    // For example, in some cases official apps use this with error 0x2A2 then uses svcBreak.

    IPC::RequestParser rp{ctx};
    u32 result = rp.Pop<u32>();

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);

    LOG_WARNING(Service_AM, "(STUBBED) called, result=0x{:08X}", result);
}

void IApplicationFunctions::GetDisplayVersion(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u64>(1);
    rb.Push<u64>(0);
    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void IApplicationFunctions::GetDesiredLanguage(Kernel::HLERequestContext& ctx) {
    // TODO(bunnei): This should be configurable
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(RESULT_SUCCESS);
    rb.Push(
        static_cast<u64>(Service::Set::GetLanguageCodeFromIndex(Settings::values.language_index)));
    LOG_DEBUG(Service_AM, "called");
}

void IApplicationFunctions::InitializeGamePlayRecording(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void IApplicationFunctions::SetGamePlayRecordingState(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);

    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void IApplicationFunctions::NotifyRunning(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u8>(0); // Unknown, seems to be ignored by official processes

    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void IApplicationFunctions::GetPseudoDeviceId(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(RESULT_SUCCESS);

    // Returns a 128-bit UUID
    rb.Push<u64>(0);
    rb.Push<u64>(0);

    LOG_WARNING(Service_AM, "(STUBBED) called");
}

void InstallInterfaces(SM::ServiceManager& service_manager,
                       std::shared_ptr<NVFlinger::NVFlinger> nvflinger) {
    std::make_shared<AppletAE>(nvflinger)->InstallAsService(service_manager);
    std::make_shared<AppletOE>(nvflinger)->InstallAsService(service_manager);
    std::make_shared<IdleSys>()->InstallAsService(service_manager);
    std::make_shared<OMM>()->InstallAsService(service_manager);
    std::make_shared<SPSM>()->InstallAsService(service_manager);
    std::make_shared<TCAP>()->InstallAsService(service_manager);
}

IHomeMenuFunctions::IHomeMenuFunctions() : ServiceFramework("IHomeMenuFunctions") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {10, &IHomeMenuFunctions::RequestToGetForeground, "RequestToGetForeground"},
        {11, nullptr, "LockForeground"},
        {12, nullptr, "UnlockForeground"},
        {20, nullptr, "PopFromGeneralChannel"},
        {21, nullptr, "GetPopFromGeneralChannelEvent"},
        {30, nullptr, "GetHomeButtonWriterLockAccessor"},
        {31, nullptr, "GetWriterLockAccessorEx"},
        {100, nullptr, "PopRequestLaunchApplicationForDebug"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IHomeMenuFunctions::~IHomeMenuFunctions() = default;

void IHomeMenuFunctions::RequestToGetForeground(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_AM, "(STUBBED) called");
}

IGlobalStateController::IGlobalStateController() : ServiceFramework("IGlobalStateController") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "RequestToEnterSleep"},
        {1, nullptr, "EnterSleep"},
        {2, nullptr, "StartSleepSequence"},
        {3, nullptr, "StartShutdownSequence"},
        {4, nullptr, "StartRebootSequence"},
        {10, nullptr, "LoadAndApplyIdlePolicySettings"},
        {11, nullptr, "NotifyCecSettingsChanged"},
        {12, nullptr, "SetDefaultHomeButtonLongPressTime"},
        {13, nullptr, "UpdateDefaultDisplayResolution"},
        {14, nullptr, "ShouldSleepOnBoot"},
        {15, nullptr, "GetHdcpAuthenticationFailedEvent"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IGlobalStateController::~IGlobalStateController() = default;

IApplicationCreator::IApplicationCreator() : ServiceFramework("IApplicationCreator") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "CreateApplication"},
        {1, nullptr, "PopLaunchRequestedApplication"},
        {10, nullptr, "CreateSystemApplication"},
        {100, nullptr, "PopFloatingApplicationForDevelopment"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IApplicationCreator::~IApplicationCreator() = default;

IProcessWindingController::IProcessWindingController()
    : ServiceFramework("IProcessWindingController") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "GetLaunchReason"},
        {11, nullptr, "OpenCallingLibraryApplet"},
        {21, nullptr, "PushContext"},
        {22, nullptr, "PopContext"},
        {23, nullptr, "CancelWindingReservation"},
        {30, nullptr, "WindAndDoReserved"},
        {40, nullptr, "ReserveToStartAndWaitAndUnwindThis"},
        {41, nullptr, "ReserveToStartAndWait"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IProcessWindingController::~IProcessWindingController() = default;
} // namespace Service::AM
