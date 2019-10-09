// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstring>
#include "audio_core/audio_renderer.h"
#include "core/core.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/savedata_factory.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/readable_event.h"
#include "core/hle/kernel/transfer_memory.h"
#include "core/hle/kernel/writable_event.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/am/applet_ae.h"
#include "core/hle/service/am/applet_oe.h"
#include "core/hle/service/am/applets/applets.h"
#include "core/hle/service/am/applets/profile_select.h"
#include "core/hle/service/am/applets/software_keyboard.h"
#include "core/hle/service/am/applets/web_browser.h"
#include "core/hle/service/am/idle.h"
#include "core/hle/service/am/omm.h"
#include "core/hle/service/am/spsm.h"
#include "core/hle/service/am/tcap.h"
#include "core/hle/service/apm/controller.h"
#include "core/hle/service/apm/interface.h"
#include "core/hle/service/bcat/backend/backend.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/ns/ns.h"
#include "core/hle/service/nvflinger/nvflinger.h"
#include "core/hle/service/pm/pm.h"
#include "core/hle/service/set/set.h"
#include "core/hle/service/sm/sm.h"
#include "core/hle/service/vi/vi.h"
#include "core/settings.h"

namespace Service::AM {

constexpr ResultCode ERR_NO_DATA_IN_CHANNEL{ErrorModule::AM, 0x2};
constexpr ResultCode ERR_NO_MESSAGES{ErrorModule::AM, 0x3};
constexpr ResultCode ERR_SIZE_OUT_OF_BOUNDS{ErrorModule::AM, 0x1F7};

enum class LaunchParameterKind : u32 {
    ApplicationSpecific = 1,
    AccountPreselectedUser = 2,
};

constexpr u32 LAUNCH_PARAMETER_ACCOUNT_PRESELECTED_USER_MAGIC = 0xC79497CA;

struct LaunchParameterAccountPreselectedUser {
    u32_le magic;
    u32_le is_account_selected;
    u128 current_user;
    INSERT_PADDING_BYTES(0x70);
};
static_assert(sizeof(LaunchParameterAccountPreselectedUser) == 0x88);

IWindowController::IWindowController(Core::System& system_)
    : ServiceFramework("IWindowController"), system{system_} {
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
    const u64 process_id = system.CurrentProcess()->GetProcessID();

    LOG_DEBUG(Service_AM, "called. Process ID=0x{:016X}", process_id);

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u64>(process_id);
}

void IWindowController::AcquireForegroundRights(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

IAudioController::IAudioController() : ServiceFramework("IAudioController") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &IAudioController::SetExpectedMasterVolume, "SetExpectedMasterVolume"},
        {1, &IAudioController::GetMainAppletExpectedMasterVolume, "GetMainAppletExpectedMasterVolume"},
        {2, &IAudioController::GetLibraryAppletExpectedMasterVolume, "GetLibraryAppletExpectedMasterVolume"},
        {3, &IAudioController::ChangeMainAppletMasterVolume, "ChangeMainAppletMasterVolume"},
        {4, &IAudioController::SetTransparentAudioRate, "SetTransparentVolumeRate"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IAudioController::~IAudioController() = default;

void IAudioController::SetExpectedMasterVolume(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const float main_applet_volume_tmp = rp.Pop<float>();
    const float library_applet_volume_tmp = rp.Pop<float>();

    LOG_DEBUG(Service_AM, "called. main_applet_volume={}, library_applet_volume={}",
              main_applet_volume_tmp, library_applet_volume_tmp);

    // Ensure the volume values remain within the 0-100% range
    main_applet_volume = std::clamp(main_applet_volume_tmp, min_allowed_volume, max_allowed_volume);
    library_applet_volume =
        std::clamp(library_applet_volume_tmp, min_allowed_volume, max_allowed_volume);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void IAudioController::GetMainAppletExpectedMasterVolume(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_AM, "called. main_applet_volume={}", main_applet_volume);
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push(main_applet_volume);
}

void IAudioController::GetLibraryAppletExpectedMasterVolume(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_AM, "called. library_applet_volume={}", library_applet_volume);
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push(library_applet_volume);
}

void IAudioController::ChangeMainAppletMasterVolume(Kernel::HLERequestContext& ctx) {
    struct Parameters {
        float volume;
        s64 fade_time_ns;
    };
    static_assert(sizeof(Parameters) == 16);

    IPC::RequestParser rp{ctx};
    const auto parameters = rp.PopRaw<Parameters>();

    LOG_DEBUG(Service_AM, "called. volume={}, fade_time_ns={}", parameters.volume,
              parameters.fade_time_ns);

    main_applet_volume = std::clamp(parameters.volume, min_allowed_volume, max_allowed_volume);
    fade_time_ns = std::chrono::nanoseconds{parameters.fade_time_ns};

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void IAudioController::SetTransparentAudioRate(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const float transparent_volume_rate_tmp = rp.Pop<float>();

    LOG_DEBUG(Service_AM, "called. transparent_volume_rate={}", transparent_volume_rate_tmp);

    // Clamp volume range to 0-100%.
    transparent_volume_rate =
        std::clamp(transparent_volume_rate_tmp, min_allowed_volume, max_allowed_volume);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
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

IDebugFunctions::IDebugFunctions() : ServiceFramework{"IDebugFunctions"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "NotifyMessageToHomeMenuForDebug"},
        {1, nullptr, "OpenMainApplication"},
        {10, nullptr, "EmulateButtonEvent"},
        {20, nullptr, "InvalidateTransitionLayer"},
        {30, nullptr, "RequestLaunchApplicationWithUserAndArgumentForDebug"},
        {40, nullptr, "GetAppletResourceUsageInfo"},
        {41, nullptr, "SetCpuBoostModeForApplet"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IDebugFunctions::~IDebugFunctions() = default;

ISelfController::ISelfController(Core::System& system,
                                 std::shared_ptr<NVFlinger::NVFlinger> nvflinger)
    : ServiceFramework("ISelfController"), system(system), nvflinger(std::move(nvflinger)) {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &ISelfController::Exit, "Exit"},
        {1, &ISelfController::LockExit, "LockExit"},
        {2, &ISelfController::UnlockExit, "UnlockExit"},
        {3, &ISelfController::EnterFatalSection, "EnterFatalSection"},
        {4, &ISelfController::LeaveFatalSection, "LeaveFatalSection"},
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
        {43, nullptr, "GetSystemSharedBufferHandle"},
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
        {68, &ISelfController::SetAutoSleepDisabled, "SetAutoSleepDisabled"},
        {69, &ISelfController::IsAutoSleepDisabled, "IsAutoSleepDisabled"},
        {70, nullptr, "ReportMultimediaError"},
        {71, nullptr, "GetCurrentIlluminanceEx"},
        {80, nullptr, "SetWirelessPriorityMode"},
        {90, &ISelfController::GetAccumulatedSuspendedTickValue, "GetAccumulatedSuspendedTickValue"},
        {91, &ISelfController::GetAccumulatedSuspendedTickChangedEvent, "GetAccumulatedSuspendedTickChangedEvent"},
        {100, nullptr, "SetAlbumImageTakenNotificationEnabled"},
        {1000, nullptr, "GetDebugStorageChannel"},
    };
    // clang-format on

    RegisterHandlers(functions);

    auto& kernel = system.Kernel();
    launchable_event = Kernel::WritableEvent::CreateEventPair(kernel, Kernel::ResetType::Manual,
                                                              "ISelfController:LaunchableEvent");

    // This event is created by AM on the first time GetAccumulatedSuspendedTickChangedEvent() is
    // called. Yuzu can just create it unconditionally, since it doesn't need to support multiple
    // ISelfControllers. The event is signaled on creation, and on transition from suspended -> not
    // suspended if the event has previously been created by a call to
    // GetAccumulatedSuspendedTickChangedEvent.
    accumulated_suspended_tick_changed_event = Kernel::WritableEvent::CreateEventPair(
        kernel, Kernel::ResetType::Manual, "ISelfController:AccumulatedSuspendedTickChangedEvent");
    accumulated_suspended_tick_changed_event.writable->Signal();
}

ISelfController::~ISelfController() = default;

void ISelfController::Exit(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_AM, "called");

    system.Shutdown();

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void ISelfController::LockExit(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_AM, "called");

    system.SetExitLock(true);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void ISelfController::UnlockExit(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_AM, "called");

    system.SetExitLock(false);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void ISelfController::EnterFatalSection(Kernel::HLERequestContext& ctx) {
    ++num_fatal_sections_entered;
    LOG_DEBUG(Service_AM, "called. Num fatal sections entered: {}", num_fatal_sections_entered);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void ISelfController::LeaveFatalSection(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_AM, "called.");

    // Entry and exit of fatal sections must be balanced.
    if (num_fatal_sections_entered == 0) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultCode{ErrorModule::AM, 512});
        return;
    }

    --num_fatal_sections_entered;

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void ISelfController::GetLibraryAppletLaunchableEvent(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    launchable_event.writable->Signal();

    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushCopyObjects(launchable_event.readable);
}

void ISelfController::SetScreenShotPermission(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void ISelfController::SetOperationModeChangedNotification(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};

    bool flag = rp.Pop<bool>();
    LOG_WARNING(Service_AM, "(STUBBED) called flag={}", flag);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void ISelfController::SetPerformanceModeChangedNotification(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};

    bool flag = rp.Pop<bool>();
    LOG_WARNING(Service_AM, "(STUBBED) called flag={}", flag);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void ISelfController::SetFocusHandlingMode(Kernel::HLERequestContext& ctx) {
    // Takes 3 input u8s with each field located immediately after the previous
    // u8, these are bool flags. No output.
    IPC::RequestParser rp{ctx};

    struct FocusHandlingModeParams {
        u8 unknown0;
        u8 unknown1;
        u8 unknown2;
    };
    const auto flags = rp.PopRaw<FocusHandlingModeParams>();

    LOG_WARNING(Service_AM, "(STUBBED) called. unknown0={}, unknown1={}, unknown2={}",
                flags.unknown0, flags.unknown1, flags.unknown2);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void ISelfController::SetRestartMessageEnabled(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void ISelfController::SetOutOfFocusSuspendingEnabled(Kernel::HLERequestContext& ctx) {
    // Takes 3 input u8s with each field located immediately after the previous
    // u8, these are bool flags. No output.
    IPC::RequestParser rp{ctx};

    bool enabled = rp.Pop<bool>();
    LOG_WARNING(Service_AM, "(STUBBED) called enabled={}", enabled);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void ISelfController::SetScreenShotImageOrientation(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void ISelfController::CreateManagedDisplayLayer(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    // TODO(Subv): Find out how AM determines the display to use, for now just
    // create the layer in the Default display.
    const auto display_id = nvflinger->OpenDisplay("Default");
    const auto layer_id = nvflinger->CreateLayer(*display_id);

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(RESULT_SUCCESS);
    rb.Push(*layer_id);
}

void ISelfController::SetHandlesRequestToDisplay(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void ISelfController::SetIdleTimeDetectionExtension(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    idle_time_detection_extension = rp.Pop<u32>();
    LOG_WARNING(Service_AM, "(STUBBED) called idle_time_detection_extension={}",
                idle_time_detection_extension);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void ISelfController::GetIdleTimeDetectionExtension(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(idle_time_detection_extension);
}

void ISelfController::SetAutoSleepDisabled(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    is_auto_sleep_disabled = rp.Pop<bool>();

    // On the system itself, if the previous state of is_auto_sleep_disabled
    // differed from the current value passed in, it'd signify the internal
    // window manager to update (and also increment some statistics like update counts)
    //
    // It'd also indicate this change to an idle handling context.
    //
    // However, given we're emulating this behavior, most of this can be ignored
    // and it's sufficient to simply set the member variable for querying via
    // IsAutoSleepDisabled().

    LOG_DEBUG(Service_AM, "called. is_auto_sleep_disabled={}", is_auto_sleep_disabled);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void ISelfController::IsAutoSleepDisabled(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_AM, "called.");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push(is_auto_sleep_disabled);
}

void ISelfController::GetAccumulatedSuspendedTickValue(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_AM, "called.");

    // This command returns the total number of system ticks since ISelfController creation
    // where the game was suspended. Since Yuzu doesn't implement game suspension, this command
    // can just always return 0 ticks.
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u64>(0);
}

void ISelfController::GetAccumulatedSuspendedTickChangedEvent(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_AM, "called.");

    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushCopyObjects(accumulated_suspended_tick_changed_event.readable);
}

AppletMessageQueue::AppletMessageQueue(Kernel::KernelCore& kernel) {
    on_new_message = Kernel::WritableEvent::CreateEventPair(kernel, Kernel::ResetType::Manual,
                                                            "AMMessageQueue:OnMessageRecieved");
    on_operation_mode_changed = Kernel::WritableEvent::CreateEventPair(
        kernel, Kernel::ResetType::Automatic, "AMMessageQueue:OperationModeChanged");
}

AppletMessageQueue::~AppletMessageQueue() = default;

const Kernel::SharedPtr<Kernel::ReadableEvent>& AppletMessageQueue::GetMesssageRecieveEvent()
    const {
    return on_new_message.readable;
}

const Kernel::SharedPtr<Kernel::ReadableEvent>& AppletMessageQueue::GetOperationModeChangedEvent()
    const {
    return on_operation_mode_changed.readable;
}

void AppletMessageQueue::PushMessage(AppletMessage msg) {
    messages.push(msg);
    on_new_message.writable->Signal();
}

AppletMessageQueue::AppletMessage AppletMessageQueue::PopMessage() {
    if (messages.empty()) {
        on_new_message.writable->Clear();
        return AppletMessage::NoMessage;
    }
    auto msg = messages.front();
    messages.pop();
    if (messages.empty()) {
        on_new_message.writable->Clear();
    }
    return msg;
}

std::size_t AppletMessageQueue::GetMessageCount() const {
    return messages.size();
}

void AppletMessageQueue::OperationModeChanged() {
    PushMessage(AppletMessage::OperationModeChanged);
    PushMessage(AppletMessage::PerformanceModeChanged);
    on_operation_mode_changed.writable->Signal();
}

void AppletMessageQueue::RequestExit() {
    PushMessage(AppletMessage::ExitRequested);
}

ICommonStateGetter::ICommonStateGetter(Core::System& system,
                                       std::shared_ptr<AppletMessageQueue> msg_queue)
    : ServiceFramework("ICommonStateGetter"), system(system), msg_queue(std::move(msg_queue)) {
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
        {53, nullptr, "BeginVrModeEx"},
        {54, nullptr, "EndVrModeEx"},
        {55, nullptr, "IsInControllerFirmwareUpdateSection"},
        {60, &ICommonStateGetter::GetDefaultDisplayResolution, "GetDefaultDisplayResolution"},
        {61, &ICommonStateGetter::GetDefaultDisplayResolutionChangeEvent, "GetDefaultDisplayResolutionChangeEvent"},
        {62, nullptr, "GetHdcpAuthenticationState"},
        {63, nullptr, "GetHdcpAuthenticationStateChangeEvent"},
        {64, nullptr, "SetTvPowerStateMatchingMode"},
        {65, nullptr, "GetApplicationIdByContentActionName"},
        {66, &ICommonStateGetter::SetCpuBoostMode, "SetCpuBoostMode"},
        {80, nullptr, "PerformSystemButtonPressingIfInFocus"},
        {90, nullptr, "SetPerformanceConfigurationChangedNotification"},
        {91, nullptr, "GetCurrentPerformanceConfiguration"},
        {200, nullptr, "GetOperationModeSystemInfo"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ICommonStateGetter::~ICommonStateGetter() = default;

void ICommonStateGetter::GetBootMode(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_AM, "called");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);

    rb.Push<u8>(static_cast<u8>(Service::PM::SystemBootMode::Normal)); // Normal boot mode
}

void ICommonStateGetter::GetEventHandle(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_AM, "called");

    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushCopyObjects(msg_queue->GetMesssageRecieveEvent());
}

void ICommonStateGetter::ReceiveMessage(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_AM, "called");

    const auto message = msg_queue->PopMessage();
    IPC::ResponseBuilder rb{ctx, 3};

    if (message == AppletMessageQueue::AppletMessage::NoMessage) {
        LOG_ERROR(Service_AM, "Message queue is empty");
        rb.Push(ERR_NO_MESSAGES);
        rb.PushEnum<AppletMessageQueue::AppletMessage>(message);
        return;
    }
    rb.Push(RESULT_SUCCESS);
    rb.PushEnum<AppletMessageQueue::AppletMessage>(message);
}

void ICommonStateGetter::GetCurrentFocusState(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push(static_cast<u8>(FocusState::InFocus));
}

void ICommonStateGetter::GetDefaultDisplayResolutionChangeEvent(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_AM, "called");

    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushCopyObjects(msg_queue->GetOperationModeChangedEvent());
}

void ICommonStateGetter::GetDefaultDisplayResolution(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_AM, "called");

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(RESULT_SUCCESS);

    if (Settings::values.use_docked_mode) {
        rb.Push(static_cast<u32>(Service::VI::DisplayResolution::DockedWidth) *
                static_cast<u32>(Settings::values.resolution_factor));
        rb.Push(static_cast<u32>(Service::VI::DisplayResolution::DockedHeight) *
                static_cast<u32>(Settings::values.resolution_factor));
    } else {
        rb.Push(static_cast<u32>(Service::VI::DisplayResolution::UndockedWidth) *
                static_cast<u32>(Settings::values.resolution_factor));
        rb.Push(static_cast<u32>(Service::VI::DisplayResolution::UndockedHeight) *
                static_cast<u32>(Settings::values.resolution_factor));
    }
}

void ICommonStateGetter::SetCpuBoostMode(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_AM, "called, forwarding to APM:SYS");

    const auto& sm = system.ServiceManager();
    const auto apm_sys = sm.GetService<APM::APM_Sys>("apm:sys");
    ASSERT(apm_sys != nullptr);

    apm_sys->SetCpuBoostMode(ctx);
}

IStorage::IStorage(std::vector<u8> buffer)
    : ServiceFramework("IStorage"), buffer(std::move(buffer)) {
    // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IStorage::Open, "Open"},
            {1, nullptr, "OpenTransferStorage"},
        };
    // clang-format on

    RegisterHandlers(functions);
}

IStorage::~IStorage() = default;

const std::vector<u8>& IStorage::GetData() const {
    return buffer;
}

void ICommonStateGetter::GetOperationMode(Kernel::HLERequestContext& ctx) {
    const bool use_docked_mode{Settings::values.use_docked_mode};
    LOG_DEBUG(Service_AM, "called, use_docked_mode={}", use_docked_mode);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push(static_cast<u8>(use_docked_mode ? OperationMode::Docked : OperationMode::Handheld));
}

void ICommonStateGetter::GetPerformanceMode(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_AM, "called");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.PushEnum(system.GetAPMController().GetCurrentPerformanceMode());
}

class ILibraryAppletAccessor final : public ServiceFramework<ILibraryAppletAccessor> {
public:
    explicit ILibraryAppletAccessor(std::shared_ptr<Applets::Applet> applet)
        : ServiceFramework("ILibraryAppletAccessor"), applet(std::move(applet)) {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &ILibraryAppletAccessor::GetAppletStateChangedEvent, "GetAppletStateChangedEvent"},
            {1, &ILibraryAppletAccessor::IsCompleted, "IsCompleted"},
            {10, &ILibraryAppletAccessor::Start, "Start"},
            {20, nullptr, "RequestExit"},
            {25, nullptr, "Terminate"},
            {30, &ILibraryAppletAccessor::GetResult, "GetResult"},
            {50, nullptr, "SetOutOfFocusApplicationSuspendingEnabled"},
            {100, &ILibraryAppletAccessor::PushInData, "PushInData"},
            {101, &ILibraryAppletAccessor::PopOutData, "PopOutData"},
            {102, nullptr, "PushExtraStorage"},
            {103, &ILibraryAppletAccessor::PushInteractiveInData, "PushInteractiveInData"},
            {104, &ILibraryAppletAccessor::PopInteractiveOutData, "PopInteractiveOutData"},
            {105, &ILibraryAppletAccessor::GetPopOutDataEvent, "GetPopOutDataEvent"},
            {106, &ILibraryAppletAccessor::GetPopInteractiveOutDataEvent, "GetPopInteractiveOutDataEvent"},
            {110, nullptr, "NeedsToExitProcess"},
            {120, nullptr, "GetLibraryAppletInfo"},
            {150, nullptr, "RequestForAppletToGetForeground"},
            {160, nullptr, "GetIndirectLayerConsumerHandle"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void GetAppletStateChangedEvent(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_AM, "called");

        const auto event = applet->GetBroker().GetStateChangedEvent();

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushCopyObjects(event);
    }

    void IsCompleted(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_AM, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(applet->TransactionComplete());
    }

    void GetResult(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_AM, "called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(applet->GetStatus());
    }

    void Start(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_AM, "called");

        ASSERT(applet != nullptr);

        applet->Initialize();
        applet->Execute();

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void PushInData(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_AM, "called");

        IPC::RequestParser rp{ctx};
        applet->GetBroker().PushNormalDataFromGame(*rp.PopIpcInterface<IStorage>());

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void PopOutData(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_AM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};

        const auto storage = applet->GetBroker().PopNormalDataToGame();
        if (storage == nullptr) {
            LOG_ERROR(Service_AM,
                      "storage is a nullptr. There is no data in the current normal channel");

            rb.Push(ERR_NO_DATA_IN_CHANNEL);
            return;
        }

        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<IStorage>(std::move(*storage));
    }

    void PushInteractiveInData(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_AM, "called");

        IPC::RequestParser rp{ctx};
        applet->GetBroker().PushInteractiveDataFromGame(*rp.PopIpcInterface<IStorage>());

        ASSERT(applet->IsInitialized());
        applet->ExecuteInteractive();
        applet->Execute();

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void PopInteractiveOutData(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_AM, "called");

        const auto storage = applet->GetBroker().PopInteractiveDataToGame();
        if (storage == nullptr) {
            LOG_ERROR(Service_AM,
                      "storage is a nullptr. There is no data in the current interactive channel");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERR_NO_DATA_IN_CHANNEL);
            return;
        }

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<IStorage>(std::move(*storage));
    }

    void GetPopOutDataEvent(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_AM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushCopyObjects(applet->GetBroker().GetNormalDataEvent());
    }

    void GetPopInteractiveOutDataEvent(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_AM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushCopyObjects(applet->GetBroker().GetInteractiveDataEvent());
    }

    std::shared_ptr<Applets::Applet> applet;
};

void IStorage::Open(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_AM, "called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};

    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<IStorageAccessor>(*this);
}

IStorageAccessor::IStorageAccessor(IStorage& storage)
    : ServiceFramework("IStorageAccessor"), backing(storage) {
    // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IStorageAccessor::GetSize, "GetSize"},
            {10, &IStorageAccessor::Write, "Write"},
            {11, &IStorageAccessor::Read, "Read"},
        };
    // clang-format on

    RegisterHandlers(functions);
}

IStorageAccessor::~IStorageAccessor() = default;

void IStorageAccessor::GetSize(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_AM, "called");

    IPC::ResponseBuilder rb{ctx, 4};

    rb.Push(RESULT_SUCCESS);
    rb.Push(static_cast<u64>(backing.buffer.size()));
}

void IStorageAccessor::Write(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};

    const u64 offset{rp.Pop<u64>()};
    const std::vector<u8> data{ctx.ReadBuffer()};

    LOG_DEBUG(Service_AM, "called, offset={}, size={}", offset, data.size());

    if (data.size() > backing.buffer.size() - offset) {
        LOG_ERROR(Service_AM,
                  "offset is out of bounds, backing_buffer_sz={}, data_size={}, offset={}",
                  backing.buffer.size(), data.size(), offset);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ERR_SIZE_OUT_OF_BOUNDS);
        return;
    }

    std::memcpy(backing.buffer.data() + offset, data.data(), data.size());

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void IStorageAccessor::Read(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};

    const u64 offset{rp.Pop<u64>()};
    const std::size_t size{ctx.GetWriteBufferSize()};

    LOG_DEBUG(Service_AM, "called, offset={}, size={}", offset, size);

    if (size > backing.buffer.size() - offset) {
        LOG_ERROR(Service_AM, "offset is out of bounds, backing_buffer_sz={}, size={}, offset={}",
                  backing.buffer.size(), size, offset);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ERR_SIZE_OUT_OF_BOUNDS);
        return;
    }

    ctx.WriteBuffer(backing.buffer.data() + offset, size);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

ILibraryAppletCreator::ILibraryAppletCreator(Core::System& system_)
    : ServiceFramework("ILibraryAppletCreator"), system{system_} {
    static const FunctionInfo functions[] = {
        {0, &ILibraryAppletCreator::CreateLibraryApplet, "CreateLibraryApplet"},
        {1, nullptr, "TerminateAllLibraryApplets"},
        {2, nullptr, "AreAnyLibraryAppletsLeft"},
        {10, &ILibraryAppletCreator::CreateStorage, "CreateStorage"},
        {11, &ILibraryAppletCreator::CreateTransferMemoryStorage, "CreateTransferMemoryStorage"},
        {12, nullptr, "CreateHandleStorage"},
    };
    RegisterHandlers(functions);
}

ILibraryAppletCreator::~ILibraryAppletCreator() = default;

void ILibraryAppletCreator::CreateLibraryApplet(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_id = rp.PopRaw<Applets::AppletId>();
    const auto applet_mode = rp.PopRaw<u32>();

    LOG_DEBUG(Service_AM, "called with applet_id={:08X}, applet_mode={:08X}",
              static_cast<u32>(applet_id), applet_mode);

    const auto& applet_manager{system.GetAppletManager()};
    const auto applet = applet_manager.GetApplet(applet_id);

    if (applet == nullptr) {
        LOG_ERROR(Service_AM, "Applet doesn't exist! applet_id={}", static_cast<u32>(applet_id));

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultCode(-1));
        return;
    }

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};

    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<AM::ILibraryAppletAccessor>(applet);
}

void ILibraryAppletCreator::CreateStorage(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const u64 size{rp.Pop<u64>()};
    LOG_DEBUG(Service_AM, "called, size={}", size);

    std::vector<u8> buffer(size);

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<AM::IStorage>(std::move(buffer));
}

void ILibraryAppletCreator::CreateTransferMemoryStorage(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_AM, "called");

    IPC::RequestParser rp{ctx};

    rp.SetCurrentOffset(3);
    const auto handle{rp.Pop<Kernel::Handle>()};

    const auto transfer_mem =
        system.CurrentProcess()->GetHandleTable().Get<Kernel::TransferMemory>(handle);

    if (transfer_mem == nullptr) {
        LOG_ERROR(Service_AM, "shared_mem is a nullpr for handle={:08X}", handle);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultCode(-1));
        return;
    }

    const u8* const mem_begin = transfer_mem->GetPointer();
    const u8* const mem_end = mem_begin + transfer_mem->GetSize();
    std::vector<u8> memory{mem_begin, mem_end};

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface(std::make_shared<IStorage>(std::move(memory)));
}

IApplicationFunctions::IApplicationFunctions(Core::System& system_)
    : ServiceFramework("IApplicationFunctions"), system{system_} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {1, &IApplicationFunctions::PopLaunchParameter, "PopLaunchParameter"},
        {10, nullptr, "CreateApplicationAndPushAndRequestToStart"},
        {11, nullptr, "CreateApplicationAndPushAndRequestToStartForQuest"},
        {12, nullptr, "CreateApplicationAndRequestToStart"},
        {13, &IApplicationFunctions::CreateApplicationAndRequestToStartForQuest, "CreateApplicationAndRequestToStartForQuest"},
        {14, nullptr, "CreateApplicationWithAttributeAndPushAndRequestToStartForQuest"},
        {15, nullptr, "CreateApplicationWithAttributeAndRequestToStartForQuest"},
        {20, &IApplicationFunctions::EnsureSaveData, "EnsureSaveData"},
        {21, &IApplicationFunctions::GetDesiredLanguage, "GetDesiredLanguage"},
        {22, &IApplicationFunctions::SetTerminateResult, "SetTerminateResult"},
        {23, &IApplicationFunctions::GetDisplayVersion, "GetDisplayVersion"},
        {24, nullptr, "GetLaunchStorageInfoForDebug"},
        {25, &IApplicationFunctions::ExtendSaveData, "ExtendSaveData"},
        {26, &IApplicationFunctions::GetSaveDataSize, "GetSaveDataSize"},
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
        {90, &IApplicationFunctions::EnableApplicationCrashReport, "EnableApplicationCrashReport"},
        {100, nullptr, "InitializeApplicationCopyrightFrameBuffer"},
        {101, nullptr, "SetApplicationCopyrightImage"},
        {102, nullptr, "SetApplicationCopyrightVisibility"},
        {110, nullptr, "QueryApplicationPlayStatistics"},
        {120, nullptr, "ExecuteProgram"},
        {121, nullptr, "ClearUserChannel"},
        {122, nullptr, "UnpopToUserChannel"},
        {130, &IApplicationFunctions::GetGpuErrorDetectedSystemEvent, "GetGpuErrorDetectedSystemEvent"},
        {500, nullptr, "StartContinuousRecordingFlushForDebug"},
        {1000, nullptr, "CreateMovieMaker"},
        {1001, nullptr, "PrepareForJit"},
    };
    // clang-format on

    RegisterHandlers(functions);

    auto& kernel = system.Kernel();
    gpu_error_detected_event = Kernel::WritableEvent::CreateEventPair(
        kernel, Kernel::ResetType::Manual, "IApplicationFunctions:GpuErrorDetectedSystemEvent");
}

IApplicationFunctions::~IApplicationFunctions() = default;

void IApplicationFunctions::EnableApplicationCrashReport(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void IApplicationFunctions::BeginBlockingHomeButtonShortAndLongPressed(
    Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void IApplicationFunctions::EndBlockingHomeButtonShortAndLongPressed(
    Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void IApplicationFunctions::BeginBlockingHomeButton(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void IApplicationFunctions::EndBlockingHomeButton(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void IApplicationFunctions::PopLaunchParameter(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto kind = rp.PopEnum<LaunchParameterKind>();

    LOG_DEBUG(Service_AM, "called, kind={:08X}", static_cast<u8>(kind));

    if (kind == LaunchParameterKind::ApplicationSpecific && !launch_popped_application_specific) {
        const auto backend = BCAT::CreateBackendFromSettings(
            [this](u64 tid) { return system.GetFileSystemController().GetBCATDirectory(tid); });
        const auto build_id_full = system.GetCurrentProcessBuildID();
        u64 build_id{};
        std::memcpy(&build_id, build_id_full.data(), sizeof(u64));

        const auto data =
            backend->GetLaunchParameter({system.CurrentProcess()->GetTitleID(), build_id});

        if (data.has_value()) {
            IPC::ResponseBuilder rb{ctx, 2, 0, 1};
            rb.Push(RESULT_SUCCESS);
            rb.PushIpcInterface<AM::IStorage>(*data);
            launch_popped_application_specific = true;
            return;
        }
    } else if (kind == LaunchParameterKind::AccountPreselectedUser &&
               !launch_popped_account_preselect) {
        LaunchParameterAccountPreselectedUser params{};

        params.magic = LAUNCH_PARAMETER_ACCOUNT_PRESELECTED_USER_MAGIC;
        params.is_account_selected = 1;

        Account::ProfileManager profile_manager{};
        const auto uuid = profile_manager.GetUser(Settings::values.current_user);
        ASSERT(uuid);
        params.current_user = uuid->uuid;

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};

        rb.Push(RESULT_SUCCESS);

        std::vector<u8> buffer(sizeof(LaunchParameterAccountPreselectedUser));
        std::memcpy(buffer.data(), &params, buffer.size());

        rb.PushIpcInterface<AM::IStorage>(buffer);
        launch_popped_account_preselect = true;
        return;
    }

    LOG_ERROR(Service_AM, "Attempted to load launch parameter but none was found!");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ERR_NO_DATA_IN_CHANNEL);
}

void IApplicationFunctions::CreateApplicationAndRequestToStartForQuest(
    Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void IApplicationFunctions::EnsureSaveData(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    u128 user_id = rp.PopRaw<u128>();

    LOG_DEBUG(Service_AM, "called, uid={:016X}{:016X}", user_id[1], user_id[0]);

    FileSys::SaveDataDescriptor descriptor{};
    descriptor.title_id = system.CurrentProcess()->GetTitleID();
    descriptor.user_id = user_id;
    descriptor.type = FileSys::SaveDataType::SaveData;
    const auto res = system.GetFileSystemController().CreateSaveData(
        FileSys::SaveDataSpaceId::NandUser, descriptor);

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(res.Code());
    rb.Push<u64>(0);
}

void IApplicationFunctions::SetTerminateResult(Kernel::HLERequestContext& ctx) {
    // Takes an input u32 Result, no output.
    // For example, in some cases official apps use this with error 0x2A2 then
    // uses svcBreak.

    IPC::RequestParser rp{ctx};
    u32 result = rp.Pop<u32>();
    LOG_WARNING(Service_AM, "(STUBBED) called, result=0x{:08X}", result);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void IApplicationFunctions::GetDisplayVersion(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u64>(1);
    rb.Push<u64>(0);
}

void IApplicationFunctions::GetDesiredLanguage(Kernel::HLERequestContext& ctx) {
    // TODO(bunnei): This should be configurable
    LOG_DEBUG(Service_AM, "called");

    // Get supported languages from NACP, if possible
    // Default to 0 (all languages supported)
    u32 supported_languages = 0;
    FileSys::PatchManager pm{system.CurrentProcess()->GetTitleID()};

    const auto res = pm.GetControlMetadata();
    if (res.first != nullptr) {
        supported_languages = res.first->GetSupportedLanguages();
    }

    // Call IApplicationManagerInterface implementation.
    auto& service_manager = system.ServiceManager();
    auto ns_am2 = service_manager.GetService<NS::NS>("ns:am2");
    auto app_man = ns_am2->GetApplicationManagerInterface();

    // Get desired application language
    const auto res_lang = app_man->GetApplicationDesiredLanguage(supported_languages);
    if (res_lang.Failed()) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(res_lang.Code());
        return;
    }

    // Convert to settings language code.
    const auto res_code = app_man->ConvertApplicationLanguageToLanguageCode(*res_lang);
    if (res_code.Failed()) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(res_code.Code());
        return;
    }

    LOG_DEBUG(Service_AM, "got desired_language={:016X}", *res_code);

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(RESULT_SUCCESS);
    rb.Push(*res_code);
}

void IApplicationFunctions::InitializeGamePlayRecording(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void IApplicationFunctions::SetGamePlayRecordingState(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void IApplicationFunctions::NotifyRunning(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u8>(0); // Unknown, seems to be ignored by official processes
}

void IApplicationFunctions::GetPseudoDeviceId(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(RESULT_SUCCESS);

    // Returns a 128-bit UUID
    rb.Push<u64>(0);
    rb.Push<u64>(0);
}

void IApplicationFunctions::ExtendSaveData(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto type{rp.PopRaw<FileSys::SaveDataType>()};
    rp.Skip(1, false);
    const auto user_id{rp.PopRaw<u128>()};
    const auto new_normal_size{rp.PopRaw<u64>()};
    const auto new_journal_size{rp.PopRaw<u64>()};

    LOG_DEBUG(Service_AM,
              "called with type={:02X}, user_id={:016X}{:016X}, new_normal={:016X}, "
              "new_journal={:016X}",
              static_cast<u8>(type), user_id[1], user_id[0], new_normal_size, new_journal_size);

    system.GetFileSystemController().WriteSaveDataSize(
        type, system.CurrentProcess()->GetTitleID(), user_id, {new_normal_size, new_journal_size});

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(RESULT_SUCCESS);

    // The following value is used upon failure to help the system recover.
    // Since we always succeed, this should be 0.
    rb.Push<u64>(0);
}

void IApplicationFunctions::GetSaveDataSize(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto type{rp.PopRaw<FileSys::SaveDataType>()};
    rp.Skip(1, false);
    const auto user_id{rp.PopRaw<u128>()};

    LOG_DEBUG(Service_AM, "called with type={:02X}, user_id={:016X}{:016X}", static_cast<u8>(type),
              user_id[1], user_id[0]);

    const auto size = system.GetFileSystemController().ReadSaveDataSize(
        type, system.CurrentProcess()->GetTitleID(), user_id);

    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(RESULT_SUCCESS);
    rb.Push(size.normal);
    rb.Push(size.journal);
}

void IApplicationFunctions::GetGpuErrorDetectedSystemEvent(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushCopyObjects(gpu_error_detected_event.readable);
}

void InstallInterfaces(SM::ServiceManager& service_manager,
                       std::shared_ptr<NVFlinger::NVFlinger> nvflinger, Core::System& system) {
    auto message_queue = std::make_shared<AppletMessageQueue>(system.Kernel());
    // Needed on game boot
    message_queue->PushMessage(AppletMessageQueue::AppletMessage::FocusStateChanged);

    std::make_shared<AppletAE>(nvflinger, message_queue, system)->InstallAsService(service_manager);
    std::make_shared<AppletOE>(nvflinger, message_queue, system)->InstallAsService(service_manager);
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
    LOG_WARNING(Service_AM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

IGlobalStateController::IGlobalStateController() : ServiceFramework("IGlobalStateController") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "RequestToEnterSleep"},
        {1, nullptr, "EnterSleep"},
        {2, nullptr, "StartSleepSequence"},
        {3, nullptr, "StartShutdownSequence"},
        {4, nullptr, "StartRebootSequence"},
        {9, nullptr, "IsAutoPowerDownRequested"},
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
