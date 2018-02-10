// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/service/apm/apm.h"
#include "core/hle/service/apm/interface.h"

namespace Service {
namespace APM {

class ISession final : public ServiceFramework<ISession> {
public:
    ISession() : ServiceFramework("ISession") {
        static const FunctionInfo functions[] = {
            {0, &ISession::SetPerformanceConfiguration, "SetPerformanceConfiguration"},
            {1, &ISession::GetPerformanceConfiguration, "GetPerformanceConfiguration"},
        };
        RegisterHandlers(functions);
    }

private:
    void SetPerformanceConfiguration(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};

        auto mode = static_cast<PerformanceMode>(rp.Pop<u32>());
        u32 config = rp.Pop<u32>();

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);

        LOG_WARNING(Service_APM, "(STUBBED) called mode=%u config=%u", static_cast<u32>(mode),
                    config);
    }

    void GetPerformanceConfiguration(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};

        auto mode = static_cast<PerformanceMode>(rp.Pop<u32>());

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(0); // Performance configuration

        LOG_WARNING(Service_APM, "(STUBBED) called mode=%u", static_cast<u32>(mode));
    }
};

APM::APM(std::shared_ptr<Module> apm, const char* name)
    : ServiceFramework(name), apm(std::move(apm)) {
    static const FunctionInfo functions[] = {
        {0, &APM::OpenSession, "OpenSession"},
        {1, nullptr, "GetPerformanceMode"},
    };
    RegisterHandlers(functions);
}

void APM::OpenSession(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<ISession>();
}

} // namespace APM
} // namespace Service
