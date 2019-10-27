// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/readable_event.h"
#include "core/hle/kernel/writable_event.h"
#include "core/hle/service/nifm/nifm.h"
#include "core/hle/service/service.h"
#include "core/settings.h"

namespace Service::NIFM {

enum class RequestState : u32 {
    NotSubmitted = 1,
    Error = 1, ///< The duplicate 1 is intentional; it means both not submitted and error on HW.
    Pending = 2,
    Connected = 3,
};

class IScanRequest final : public ServiceFramework<IScanRequest> {
public:
    explicit IScanRequest() : ServiceFramework("IScanRequest") {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "Submit"},
            {1, nullptr, "IsProcessing"},
            {2, nullptr, "GetResult"},
            {3, nullptr, "GetSystemEventReadableHandle"},
            {4, nullptr, "SetChannels"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IRequest final : public ServiceFramework<IRequest> {
public:
    explicit IRequest(Core::System& system) : ServiceFramework("IRequest") {
        static const FunctionInfo functions[] = {
            {0, &IRequest::GetRequestState, "GetRequestState"},
            {1, &IRequest::GetResult, "GetResult"},
            {2, &IRequest::GetSystemEventReadableHandles, "GetSystemEventReadableHandles"},
            {3, &IRequest::Cancel, "Cancel"},
            {4, &IRequest::Submit, "Submit"},
            {5, nullptr, "SetRequirement"},
            {6, nullptr, "SetRequirementPreset"},
            {8, nullptr, "SetPriority"},
            {9, nullptr, "SetNetworkProfileId"},
            {10, nullptr, "SetRejectable"},
            {11, &IRequest::SetConnectionConfirmationOption, "SetConnectionConfirmationOption"},
            {12, nullptr, "SetPersistent"},
            {13, nullptr, "SetInstant"},
            {14, nullptr, "SetSustainable"},
            {15, nullptr, "SetRawPriority"},
            {16, nullptr, "SetGreedy"},
            {17, nullptr, "SetSharable"},
            {18, nullptr, "SetRequirementByRevision"},
            {19, nullptr, "GetRequirement"},
            {20, nullptr, "GetRevision"},
            {21, nullptr, "GetAppletInfo"},
            {22, nullptr, "GetAdditionalInfo"},
            {23, nullptr, "SetKeptInSleep"},
            {24, nullptr, "RegisterSocketDescriptor"},
            {25, nullptr, "UnregisterSocketDescriptor"},
        };
        RegisterHandlers(functions);

        auto& kernel = system.Kernel();
        event1 = Kernel::WritableEvent::CreateEventPair(kernel, Kernel::ResetType::Automatic,
                                                        "IRequest:Event1");
        event2 = Kernel::WritableEvent::CreateEventPair(kernel, Kernel::ResetType::Automatic,
                                                        "IRequest:Event2");
    }

private:
    void Submit(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void GetRequestState(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);

        if (Settings::values.bcat_backend == "none") {
            rb.PushEnum(RequestState::NotSubmitted);
        } else {
            rb.PushEnum(RequestState::Connected);
        }
    }

    void GetResult(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void GetSystemEventReadableHandles(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2, 2};
        rb.Push(RESULT_SUCCESS);
        rb.PushCopyObjects(event1.readable, event2.readable);
    }

    void Cancel(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void SetConnectionConfirmationOption(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    Kernel::EventPair event1, event2;
};

class INetworkProfile final : public ServiceFramework<INetworkProfile> {
public:
    explicit INetworkProfile() : ServiceFramework("INetworkProfile") {
        static const FunctionInfo functions[] = {
            {0, nullptr, "Update"},
            {1, nullptr, "PersistOld"},
            {2, nullptr, "Persist"},
        };
        RegisterHandlers(functions);
    }
};

class IGeneralService final : public ServiceFramework<IGeneralService> {
public:
    IGeneralService(Core::System& system);

private:
    void GetClientId(Kernel::HLERequestContext& ctx) {
        static constexpr u32 client_id = 1;
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u64>(client_id); // Client ID needs to be non zero otherwise it's considered invalid
    }
    void CreateScanRequest(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIFM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};

        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<IScanRequest>();
    }
    void CreateRequest(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIFM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};

        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<IRequest>(system);
    }
    void RemoveNetworkProfile(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }
    void CreateTemporaryNetworkProfile(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIFM, "called");

        ASSERT_MSG(ctx.GetReadBufferSize() == 0x17c, "NetworkProfileData is not the correct size");
        u128 uuid{};
        auto buffer = ctx.ReadBuffer();
        std::memcpy(&uuid, buffer.data() + 8, sizeof(u128));

        IPC::ResponseBuilder rb{ctx, 6, 0, 1};

        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<INetworkProfile>();
        rb.PushRaw<u128>(uuid);
    }
    void IsWirelessCommunicationEnabled(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u8>(0);
    }
    void IsEthernetCommunicationEnabled(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        if (Settings::values.bcat_backend == "none") {
            rb.Push<u8>(0);
        } else {
            rb.Push<u8>(1);
        }
    }
    void IsAnyInternetRequestAccepted(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_NIFM, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        if (Settings::values.bcat_backend == "none") {
            rb.Push<u8>(0);
        } else {
            rb.Push<u8>(1);
        }
    }
    Core::System& system;
};

IGeneralService::IGeneralService(Core::System& system)
    : ServiceFramework("IGeneralService"), system(system) {
    static const FunctionInfo functions[] = {
        {1, &IGeneralService::GetClientId, "GetClientId"},
        {2, &IGeneralService::CreateScanRequest, "CreateScanRequest"},
        {4, &IGeneralService::CreateRequest, "CreateRequest"},
        {5, nullptr, "GetCurrentNetworkProfile"},
        {6, nullptr, "EnumerateNetworkInterfaces"},
        {7, nullptr, "EnumerateNetworkProfiles"},
        {8, nullptr, "GetNetworkProfile"},
        {9, nullptr, "SetNetworkProfile"},
        {10, &IGeneralService::RemoveNetworkProfile, "RemoveNetworkProfile"},
        {11, nullptr, "GetScanDataOld"},
        {12, nullptr, "GetCurrentIpAddress"},
        {13, nullptr, "GetCurrentAccessPointOld"},
        {14, &IGeneralService::CreateTemporaryNetworkProfile, "CreateTemporaryNetworkProfile"},
        {15, nullptr, "GetCurrentIpConfigInfo"},
        {16, nullptr, "SetWirelessCommunicationEnabled"},
        {17, &IGeneralService::IsWirelessCommunicationEnabled, "IsWirelessCommunicationEnabled"},
        {18, nullptr, "GetInternetConnectionStatus"},
        {19, nullptr, "SetEthernetCommunicationEnabled"},
        {20, &IGeneralService::IsEthernetCommunicationEnabled, "IsEthernetCommunicationEnabled"},
        {21, &IGeneralService::IsAnyInternetRequestAccepted, "IsAnyInternetRequestAccepted"},
        {22, nullptr, "IsAnyForegroundRequestAccepted"},
        {23, nullptr, "PutToSleep"},
        {24, nullptr, "WakeUp"},
        {25, nullptr, "GetSsidListVersion"},
        {26, nullptr, "SetExclusiveClient"},
        {27, nullptr, "GetDefaultIpSetting"},
        {28, nullptr, "SetDefaultIpSetting"},
        {29, nullptr, "SetWirelessCommunicationEnabledForTest"},
        {30, nullptr, "SetEthernetCommunicationEnabledForTest"},
        {31, nullptr, "GetTelemetorySystemEventReadableHandle"},
        {32, nullptr, "GetTelemetryInfo"},
        {33, nullptr, "ConfirmSystemAvailability"},
        {34, nullptr, "SetBackgroundRequestEnabled"},
        {35, nullptr, "GetScanData"},
        {36, nullptr, "GetCurrentAccessPoint"},
        {37, nullptr, "Shutdown"},
        {38, nullptr, "GetAllowedChannels"},
    };
    RegisterHandlers(functions);
}

class NetworkInterface final : public ServiceFramework<NetworkInterface> {
public:
    explicit NetworkInterface(const char* name, Core::System& system)
        : ServiceFramework{name}, system(system) {
        static const FunctionInfo functions[] = {
            {4, &NetworkInterface::CreateGeneralServiceOld, "CreateGeneralServiceOld"},
            {5, &NetworkInterface::CreateGeneralService, "CreateGeneralService"},
        };
        RegisterHandlers(functions);
    }

    void CreateGeneralServiceOld(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIFM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<IGeneralService>(system);
    }

    void CreateGeneralService(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_NIFM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<IGeneralService>(system);
    }

private:
    Core::System& system;
};

void InstallInterfaces(SM::ServiceManager& service_manager, Core::System& system) {
    std::make_shared<NetworkInterface>("nifm:a", system)->InstallAsService(service_manager);
    std::make_shared<NetworkInterface>("nifm:s", system)->InstallAsService(service_manager);
    std::make_shared<NetworkInterface>("nifm:u", system)->InstallAsService(service_manager);
}

} // namespace Service::NIFM
