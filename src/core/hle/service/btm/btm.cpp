// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>

#include "common/logging/log.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/event.h"
#include "core/hle/kernel/hle_ipc.h"
#include "core/hle/service/btm/btm.h"
#include "core/hle/service/service.h"

namespace Service::BTM {

class IBtmUserCore final : public ServiceFramework<IBtmUserCore> {
public:
    explicit IBtmUserCore() : ServiceFramework{"IBtmUserCore"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IBtmUserCore::GetScanEvent, "GetScanEvent"},
            {1, nullptr, "Unknown1"},
            {2, nullptr, "Unknown2"},
            {3, nullptr, "Unknown3"},
            {4, nullptr, "Unknown4"},
            {5, nullptr, "Unknown5"},
            {6, nullptr, "Unknown6"},
            {7, nullptr, "Unknown7"},
            {8, nullptr, "Unknown8"},
            {9, nullptr, "Unknown9"},
            {10, nullptr, "Unknown10"},
            {17, &IBtmUserCore::GetConnectionEvent, "GetConnectionEvent"},
            {18, nullptr, "Unknown18"},
            {19, nullptr, "Unknown19"},
            {20, nullptr, "Unknown20"},
            {21, nullptr, "Unknown21"},
            {22, nullptr, "Unknown22"},
            {23, nullptr, "Unknown23"},
            {24, nullptr, "Unknown24"},
            {25, nullptr, "Unknown25"},
            {26, &IBtmUserCore::GetDiscoveryEvent, "AcquireBleServiceDiscoveryEventImpl"},
            {27, nullptr, "Unknown27"},
            {28, nullptr, "Unknown28"},
            {29, nullptr, "Unknown29"},
            {30, nullptr, "Unknown30"},
            {31, nullptr, "Unknown31"},
            {32, nullptr, "Unknown32"},
            {33, &IBtmUserCore::GetConfigEvent, "GetConfigEvent"},
            {34, nullptr, "Unknown34"},
            {35, nullptr, "Unknown35"},
            {36, nullptr, "Unknown36"},
            {37, nullptr, "Unknown37"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }

private:
    void GetScanEvent(Kernel::HLERequestContext& ctx) {
        auto& kernel = Core::System::GetInstance().Kernel();
        scan_event =
            Kernel::Event::Create(kernel, Kernel::ResetType::OneShot, "IBtmUserCore:ScanEvent");
        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushCopyObjects(scan_event);
        LOG_WARNING(Service_BTM, "(STUBBED) called");
    }
    void GetConnectionEvent(Kernel::HLERequestContext& ctx) {
        auto& kernel = Core::System::GetInstance().Kernel();
        connection_event = Kernel::Event::Create(kernel, Kernel::ResetType::OneShot,
                                                 "IBtmUserCore:ConnectionEvent");
        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushCopyObjects(connection_event);
        LOG_WARNING(Service_BTM, "(STUBBED) called");
    }
    void GetDiscoveryEvent(Kernel::HLERequestContext& ctx) {
        auto& kernel = Core::System::GetInstance().Kernel();
        service_discovery =
            Kernel::Event::Create(kernel, Kernel::ResetType::OneShot, "IBtmUserCore:Discovery");
        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushCopyObjects(service_discovery);
        LOG_WARNING(Service_BTM, "(STUBBED) called");
    }
    void GetConfigEvent(Kernel::HLERequestContext& ctx) {
        auto& kernel = Core::System::GetInstance().Kernel();
        config_event =
            Kernel::Event::Create(kernel, Kernel::ResetType::OneShot, "IBtmUserCore:ConfigEvent");
        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushCopyObjects(config_event);
        LOG_WARNING(Service_BTM, "(STUBBED) called");
    }
    Kernel::SharedPtr<Kernel::Event> scan_event;
    Kernel::SharedPtr<Kernel::Event> connection_event;
    Kernel::SharedPtr<Kernel::Event> service_discovery;
    Kernel::SharedPtr<Kernel::Event> config_event;
};

class BTM_USR final : public ServiceFramework<BTM_USR> {
public:
    explicit BTM_USR() : ServiceFramework{"btm:u"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &BTM_USR::GetCoreImpl, "GetCoreImpl"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }

private:
    void GetCoreImpl(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<IBtmUserCore>();
        LOG_DEBUG(Service_BTM, "called");
    }
};

class BTM final : public ServiceFramework<BTM> {
public:
    explicit BTM() : ServiceFramework{"btm"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "Unknown1"},
            {1, nullptr, "Unknown2"},
            {2, nullptr, "RegisterSystemEventForConnectedDeviceConditionImpl"},
            {3, nullptr, "Unknown3"},
            {4, nullptr, "Unknown4"},
            {5, nullptr, "Unknown5"},
            {6, nullptr, "Unknown6"},
            {7, nullptr, "Unknown7"},
            {8, nullptr, "RegisterSystemEventForRegisteredDeviceInfoImpl"},
            {9, nullptr, "Unknown8"},
            {10, nullptr, "Unknown9"},
            {11, nullptr, "Unknown10"},
            {12, nullptr, "Unknown11"},
            {13, nullptr, "Unknown12"},
            {14, nullptr, "EnableRadioImpl"},
            {15, nullptr, "DisableRadioImpl"},
            {16, nullptr, "Unknown13"},
            {17, nullptr, "Unknown14"},
            {18, nullptr, "Unknown15"},
            {19, nullptr, "Unknown16"},
            {20, nullptr, "Unknown17"},
            {21, nullptr, "Unknown18"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class BTM_DBG final : public ServiceFramework<BTM_DBG> {
public:
    explicit BTM_DBG() : ServiceFramework{"btm:dbg"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "RegisterSystemEventForDiscoveryImpl"},
            {1, nullptr, "Unknown1"},
            {2, nullptr, "Unknown2"},
            {3, nullptr, "Unknown3"},
            {4, nullptr, "Unknown4"},
            {5, nullptr, "Unknown5"},
            {6, nullptr, "Unknown6"},
            {7, nullptr, "Unknown7"},
            {8, nullptr, "Unknown8"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IBtmSystemCore final : public ServiceFramework<IBtmSystemCore> {
public:
    explicit IBtmSystemCore() : ServiceFramework{"IBtmSystemCore"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "StartGamepadPairingImpl"},
            {1, nullptr, "CancelGamepadPairingImpl"},
            {2, nullptr, "ClearGamepadPairingDatabaseImpl"},
            {3, nullptr, "GetPairedGamepadCountImpl"},
            {4, nullptr, "EnableRadioImpl"},
            {5, nullptr, "DisableRadioImpl"},
            {6, nullptr, "GetRadioOnOffImpl"},
            {7, nullptr, "AcquireRadioEventImpl"},
            {8, nullptr, "AcquireGamepadPairingEventImpl"},
            {9, nullptr, "IsGamepadPairingStartedImpl"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class BTM_SYS final : public ServiceFramework<BTM_SYS> {
public:
    explicit BTM_SYS() : ServiceFramework{"btm:sys"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &BTM_SYS::GetCoreImpl, "GetCoreImpl"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void GetCoreImpl(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<IBtmSystemCore>();

        LOG_DEBUG(Service_BTM, "called");
    }
};

void InstallInterfaces(SM::ServiceManager& sm) {
    std::make_shared<BTM>()->InstallAsService(sm);
    std::make_shared<BTM_DBG>()->InstallAsService(sm);
    std::make_shared<BTM_SYS>()->InstallAsService(sm);
    std::make_shared<BTM_USR>()->InstallAsService(sm);
}

} // namespace Service::BTM
