// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/event.h"
#include "core/hle/service/hid/hid.h"
#include "core/hle/service/nfp/nfp.h"
#include "core/hle/service/nfp/nfp_user.h"

namespace Service::NFP {

Module::Interface::Interface(std::shared_ptr<Module> module, const char* name)
    : ServiceFramework(name), module(std::move(module)) {}

Module::Interface::~Interface() = default;

class IUser final : public ServiceFramework<IUser> {
public:
    IUser() : ServiceFramework("IUser") {
        static const FunctionInfo functions[] = {
            {0, &IUser::Initialize, "Initialize"},
            {1, nullptr, "Finalize"},
            {2, &IUser::ListDevices, "ListDevices"},
            {3, nullptr, "StartDetection"},
            {4, nullptr, "StopDetection"},
            {5, nullptr, "Mount"},
            {6, nullptr, "Unmount"},
            {7, nullptr, "OpenApplicationArea"},
            {8, nullptr, "GetApplicationArea"},
            {9, nullptr, "SetApplicationArea"},
            {10, nullptr, "Flush"},
            {11, nullptr, "Restore"},
            {12, nullptr, "CreateApplicationArea"},
            {13, nullptr, "GetTagInfo"},
            {14, nullptr, "GetRegisterInfo"},
            {15, nullptr, "GetCommonInfo"},
            {16, nullptr, "GetModelInfo"},
            {17, &IUser::AttachActivateEvent, "AttachActivateEvent"},
            {18, &IUser::AttachDeactivateEvent, "AttachDeactivateEvent"},
            {19, &IUser::GetState, "GetState"},
            {20, &IUser::GetDeviceState, "GetDeviceState"},
            {21, &IUser::GetNpadId, "GetNpadId"},
            {22, nullptr, "GetApplicationArea2"},
            {23, &IUser::AttachAvailabilityChangeEvent, "AttachAvailabilityChangeEvent"},
            {24, nullptr, "RecreateApplicationArea"},
        };
        RegisterHandlers(functions);

        auto& kernel = Core::System::GetInstance().Kernel();
        activate_event =
            Kernel::Event::Create(kernel, Kernel::ResetType::OneShot, "IUser:ActivateEvent");
        deactivate_event =
            Kernel::Event::Create(kernel, Kernel::ResetType::OneShot, "IUser:DeactivateEvent");
        availability_change_event = Kernel::Event::Create(kernel, Kernel::ResetType::OneShot,
                                                          "IUser:AvailabilityChangeEvent");
    }

private:
    enum class State : u32 {
        NonInitialized = 0,
        Initialized = 1,
    };

    enum class DeviceState : u32 {
        Initialized = 0,
    };

    void Initialize(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_NFP, "(STUBBED) called");

        state = State::Initialized;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void ListDevices(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u32 array_size = rp.Pop<u32>();

        ctx.WriteBuffer(&device_handle, sizeof(device_handle));

        LOG_WARNING(Service_NFP, "(STUBBED) called, array_size={}", array_size);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(0);
    }

    void AttachActivateEvent(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u64 dev_handle = rp.Pop<u64>();
        LOG_WARNING(Service_NFP, "(STUBBED) called, dev_handle=0x{:X}", dev_handle);

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushCopyObjects(activate_event);
    }

    void AttachDeactivateEvent(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u64 dev_handle = rp.Pop<u64>();
        LOG_WARNING(Service_NFP, "(STUBBED) called, dev_handle=0x{:X}", dev_handle);

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushCopyObjects(deactivate_event);
    }

    void GetState(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_NFP, "(STUBBED) called");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(static_cast<u32>(state));
    }

    void GetDeviceState(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_NFP, "(STUBBED) called");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(static_cast<u32>(device_state));
    }

    void GetNpadId(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u64 dev_handle = rp.Pop<u64>();
        LOG_WARNING(Service_NFP, "(STUBBED) called, dev_handle=0x{:X}", dev_handle);
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(npad_id);
    }

    void AttachAvailabilityChangeEvent(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u64 dev_handle = rp.Pop<u64>();
        LOG_WARNING(Service_NFP, "(STUBBED) called, dev_handle=0x{:X}", dev_handle);

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushCopyObjects(availability_change_event);
    }

    const u64 device_handle{0xDEAD};
    const u32 npad_id{0}; // This is the first player controller id
    State state{State::NonInitialized};
    DeviceState device_state{DeviceState::Initialized};
    Kernel::SharedPtr<Kernel::Event> activate_event;
    Kernel::SharedPtr<Kernel::Event> deactivate_event;
    Kernel::SharedPtr<Kernel::Event> availability_change_event;
};

void Module::Interface::CreateUserInterface(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_NFP, "called");
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<IUser>();
}

void InstallInterfaces(SM::ServiceManager& service_manager) {
    auto module = std::make_shared<Module>();
    std::make_shared<NFP_User>(module)->InstallAsService(service_manager);
}

} // namespace Service::NFP
