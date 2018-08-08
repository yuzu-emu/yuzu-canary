// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include "core/hle/service/service.h"

namespace Kernel {
class Event;
}

namespace Service {
namespace NVFlinger {
class NVFlinger;
}

namespace AM {

enum SystemLanguage {
    Japanese = 0,
    English = 1, // en-US
    French = 2,
    German = 3,
    Italian = 4,
    Spanish = 5,
    Chinese = 6,
    Korean = 7,
    Dutch = 8,
    Portuguese = 9,
    Russian = 10,
    Taiwanese = 11,
    BritishEnglish = 12, // en-GB
    CanadianFrench = 13,
    LatinAmericanSpanish = 14, // es-419
    // 4.0.0+
    SimplifiedChinese = 15,
    TraditionalChinese = 16,
};

class IWindowController final : public ServiceFramework<IWindowController> {
public:
    IWindowController();

private:
    void GetAppletResourceUserId(Kernel::HLERequestContext& ctx);
    void AcquireForegroundRights(Kernel::HLERequestContext& ctx);
};

class IAudioController final : public ServiceFramework<IAudioController> {
public:
    IAudioController();

private:
    void SetExpectedMasterVolume(Kernel::HLERequestContext& ctx);
    void GetMainAppletExpectedMasterVolume(Kernel::HLERequestContext& ctx);
    void GetLibraryAppletExpectedMasterVolume(Kernel::HLERequestContext& ctx);

    u32 volume{100};
};

class IDisplayController final : public ServiceFramework<IDisplayController> {
public:
    IDisplayController();
};

class IDebugFunctions final : public ServiceFramework<IDebugFunctions> {
public:
    IDebugFunctions();
};

class ISelfController final : public ServiceFramework<ISelfController> {
public:
    explicit ISelfController(std::shared_ptr<NVFlinger::NVFlinger> nvflinger);

private:
    void SetFocusHandlingMode(Kernel::HLERequestContext& ctx);
    void SetRestartMessageEnabled(Kernel::HLERequestContext& ctx);
    void SetPerformanceModeChangedNotification(Kernel::HLERequestContext& ctx);
    void SetOperationModeChangedNotification(Kernel::HLERequestContext& ctx);
    void SetOutOfFocusSuspendingEnabled(Kernel::HLERequestContext& ctx);
    void LockExit(Kernel::HLERequestContext& ctx);
    void UnlockExit(Kernel::HLERequestContext& ctx);
    void GetLibraryAppletLaunchableEvent(Kernel::HLERequestContext& ctx);
    void SetScreenShotImageOrientation(Kernel::HLERequestContext& ctx);
    void CreateManagedDisplayLayer(Kernel::HLERequestContext& ctx);
    void SetScreenShotPermission(Kernel::HLERequestContext& ctx);
    void SetHandlesRequestToDisplay(Kernel::HLERequestContext& ctx);

    std::shared_ptr<NVFlinger::NVFlinger> nvflinger;
    Kernel::SharedPtr<Kernel::Event> launchable_event;
};

class ICommonStateGetter final : public ServiceFramework<ICommonStateGetter> {
public:
    ICommonStateGetter();

private:
    enum class FocusState : u8 {
        InFocus = 1,
        NotInFocus = 2,
    };

    enum class OperationMode : u8 {
        Handheld = 0,
        Docked = 1,
    };

    void GetEventHandle(Kernel::HLERequestContext& ctx);
    void ReceiveMessage(Kernel::HLERequestContext& ctx);
    void GetCurrentFocusState(Kernel::HLERequestContext& ctx);
    void GetOperationMode(Kernel::HLERequestContext& ctx);
    void GetPerformanceMode(Kernel::HLERequestContext& ctx);

    Kernel::SharedPtr<Kernel::Event> event;
};

class ILibraryAppletCreator final : public ServiceFramework<ILibraryAppletCreator> {
public:
    ILibraryAppletCreator();

private:
    void CreateLibraryApplet(Kernel::HLERequestContext& ctx);
    void CreateStorage(Kernel::HLERequestContext& ctx);
};

class IApplicationFunctions final : public ServiceFramework<IApplicationFunctions> {
public:
    IApplicationFunctions();

private:
    void PopLaunchParameter(Kernel::HLERequestContext& ctx);
    void CreateApplicationAndRequestToStartForQuest(Kernel::HLERequestContext& ctx);
    void EnsureSaveData(Kernel::HLERequestContext& ctx);
    void SetTerminateResult(Kernel::HLERequestContext& ctx);
    void GetDisplayVersion(Kernel::HLERequestContext& ctx);
    void GetDesiredLanguage(Kernel::HLERequestContext& ctx);
    void InitializeGamePlayRecording(Kernel::HLERequestContext& ctx);
    void SetGamePlayRecordingState(Kernel::HLERequestContext& ctx);
    void NotifyRunning(Kernel::HLERequestContext& ctx);
    void GetPseudoDeviceId(Kernel::HLERequestContext& ctx);
};

class IHomeMenuFunctions final : public ServiceFramework<IHomeMenuFunctions> {
public:
    IHomeMenuFunctions();

private:
    void RequestToGetForeground(Kernel::HLERequestContext& ctx);
};

class IGlobalStateController final : public ServiceFramework<IGlobalStateController> {
public:
    IGlobalStateController();
};

class IApplicationCreator final : public ServiceFramework<IApplicationCreator> {
public:
    IApplicationCreator();
};

class IProcessWindingController final : public ServiceFramework<IProcessWindingController> {
public:
    IProcessWindingController();
};

/// Registers all AM services with the specified service manager.
void InstallInterfaces(SM::ServiceManager& service_manager,
                       std::shared_ptr<NVFlinger::NVFlinger> nvflinger);

} // namespace AM
} // namespace Service
