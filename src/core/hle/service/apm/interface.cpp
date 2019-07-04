// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/service/apm/apm.h"
#include "core/hle/service/apm/controller.h"
#include "core/hle/service/apm/interface.h"

namespace Service::APM {

class ISession final : public ServiceFramework<ISession> {
public:
    ISession(Controller& controller) : ServiceFramework("ISession"), controller(controller) {
        static const FunctionInfo functions[] = {
            {0, &ISession::SetPerformanceConfiguration, "SetPerformanceConfiguration"},
            {1, &ISession::GetPerformanceConfiguration, "GetPerformanceConfiguration"},
            {2, nullptr, "SetCpuOverclockEnabled"},
        };
        RegisterHandlers(functions);
    }

private:
    void SetPerformanceConfiguration(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};

        const auto mode = rp.PopEnum<PerformanceMode>();
        const auto config = rp.PopEnum<PerformanceConfiguration>();
        LOG_DEBUG(Service_APM, "called mode={} config={}", static_cast<u32>(mode),
                  static_cast<u32>(config));

        controller.SetPerformanceConfiguration(mode, config);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void GetPerformanceConfiguration(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};

        const auto mode = rp.PopEnum<PerformanceMode>();
        LOG_DEBUG(Service_APM, "called mode={}", static_cast<u32>(mode));

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.PushEnum(controller.GetCurrentPerformanceConfiguration(mode));
    }

    Controller& controller;
};

APM::APM(std::shared_ptr<Module> apm, Controller& controller, const char* name)
    : ServiceFramework(name), apm(std::move(apm)), controller(controller) {
    static const FunctionInfo functions[] = {
        {0, &APM::OpenSession, "OpenSession"},
        {1, &APM::GetPerformanceMode, "GetPerformanceMode"},
        {6, nullptr, "IsCpuOverclockEnabled"},
    };
    RegisterHandlers(functions);
}

APM::~APM() = default;

void APM::OpenSession(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_APM, "called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<ISession>(controller);
}

void APM::GetPerformanceMode(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_APM, "called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.PushEnum(controller.GetCurrentPerformanceMode());
}

APM_Sys::APM_Sys(Controller& controller) : ServiceFramework{"apm:sys"}, controller(controller) {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "RequestPerformanceMode"},
        {1, &APM_Sys::GetPerformanceEvent, "GetPerformanceEvent"},
        {2, nullptr, "GetThrottlingState"},
        {3, nullptr, "GetLastThrottlingState"},
        {4, nullptr, "ClearLastThrottlingState"},
        {5, nullptr, "LoadAndApplySettings"},
        {6, &APM_Sys::SetCpuBoostMode, "SetCpuBoostMode"},
        {7, &APM_Sys::GetCurrentPerformanceConfiguration, "GetCurrentPerformanceConfiguration"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

APM_Sys::~APM_Sys() = default;

void APM_Sys::GetPerformanceEvent(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_APM, "called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<ISession>(controller);
}

void APM_Sys::SetCpuBoostMode(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto mode = rp.PopEnum<CpuBoostMode>();

    LOG_DEBUG(Service_APM, "called, mode={:08X}", static_cast<u32>(mode));

    controller.SetFromCpuBoostMode(mode);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void APM_Sys::GetCurrentPerformanceConfiguration(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_APM, "called");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.PushEnum(
        controller.GetCurrentPerformanceConfiguration(controller.GetCurrentPerformanceMode()));
}

} // namespace Service::APM
