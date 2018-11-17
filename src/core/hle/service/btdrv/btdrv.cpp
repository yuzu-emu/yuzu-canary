// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/event.h"
#include "core/hle/kernel/hle_ipc.h"
#include "core/hle/service/btdrv/btdrv.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/sm.h"

namespace Service::BtDrv {

class Bt final : public ServiceFramework<Bt> {
public:
    explicit Bt() : ServiceFramework{"bt"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "Unknown0"},
            {1, nullptr, "Unknown1"},
            {2, nullptr, "Unknown2"},
            {3, nullptr, "Unknown3"},
            {4, nullptr, "Unknown4"},
            {5, nullptr, "Unknown5"},
            {6, nullptr, "Unknown6"},
            {7, nullptr, "Unknown7"},
            {8, nullptr, "Unknown8"},
            {9, &Bt::RegisterEvent, "RegisterEvent"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }

private:
    void RegisterEvent(Kernel::HLERequestContext& ctx) {
        auto& kernel = Core::System::GetInstance().Kernel();
        register_event =
            Kernel::Event::Create(kernel, Kernel::ResetType::OneShot, "BT:RegisterEvent");
        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushCopyObjects(register_event);
        LOG_WARNING(Service_BTM, "(STUBBED) called");
    }
    Kernel::SharedPtr<Kernel::Event> register_event;
};

class BtDrv final : public ServiceFramework<BtDrv> {
public:
    explicit BtDrv() : ServiceFramework{"btdrv"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "Unknown"},
            {1, nullptr, "Init"},
            {2, nullptr, "Enable"},
            {3, nullptr, "Disable"},
            {4, nullptr, "CleanupAndShutdown"},
            {5, nullptr, "GetAdapterProperties"},
            {6, nullptr, "GetAdapterProperty"},
            {7, nullptr, "SetAdapterProperty"},
            {8, nullptr, "StartDiscovery"},
            {9, nullptr, "CancelDiscovery"},
            {10, nullptr, "CreateBond"},
            {11, nullptr, "RemoveBond"},
            {12, nullptr, "CancelBond"},
            {13, nullptr, "PinReply"},
            {14, nullptr, "SspReply"},
            {15, nullptr, "Unknown2"},
            {16, nullptr, "InitInterfaces"},
            {17, nullptr, "HidHostInterface_Connect"},
            {18, nullptr, "HidHostInterface_Disconnect"},
            {19, nullptr, "HidHostInterface_SendData"},
            {20, nullptr, "HidHostInterface_SendData2"},
            {21, nullptr, "HidHostInterface_SetReport"},
            {22, nullptr, "HidHostInterface_GetReport"},
            {23, nullptr, "HidHostInterface_WakeController"},
            {24, nullptr, "HidHostInterface_AddPairedDevice"},
            {25, nullptr, "HidHostInterface_GetPairedDevice"},
            {26, nullptr, "HidHostInterface_CleanupAndShutdown"},
            {27, nullptr, "Unknown3"},
            {28, nullptr, "ExtInterface_SetTSI"},
            {29, nullptr, "ExtInterface_SetBurstMode"},
            {30, nullptr, "ExtInterface_SetZeroRetran"},
            {31, nullptr, "ExtInterface_SetMcMode"},
            {32, nullptr, "ExtInterface_StartLlrMode"},
            {33, nullptr, "ExtInterface_ExitLlrMode"},
            {34, nullptr, "ExtInterface_SetRadio"},
            {35, nullptr, "ExtInterface_SetVisibility"},
            {36, nullptr, "Unknown4"},
            {37, nullptr, "Unknown5"},
            {38, nullptr, "HidHostInterface_GetLatestPlr"},
            {39, nullptr, "ExtInterface_GetPendingConnections"},
            {40, nullptr, "HidHostInterface_GetChannelMap"},
            {41, nullptr, "SetIsBluetoothBoostEnabled"},
            {42, nullptr, "GetIsBluetoothBoostEnabled"},
            {43, nullptr, "SetIsBluetoothAfhEnabled"},
            {44, nullptr, "GetIsBluetoothAfhEnabled"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

void InstallInterfaces(SM::ServiceManager& sm) {
    std::make_shared<BtDrv>()->InstallAsService(sm);
    std::make_shared<Bt>()->InstallAsService(sm);
}

} // namespace Service::BtDrv
