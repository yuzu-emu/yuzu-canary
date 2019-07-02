// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/am/applet_oe.h"
#include "core/hle/service/nvflinger/nvflinger.h"

namespace Service::AM {

class IApplicationProxy final : public ServiceFramework<IApplicationProxy> {
public:
    explicit IApplicationProxy(std::shared_ptr<NVFlinger::NVFlinger> nvflinger,
                               std::shared_ptr<AppletMessageQueue> msg_queue, Core::System& system)
        : ServiceFramework("IApplicationProxy"), nvflinger(std::move(nvflinger)),
          msg_queue(std::move(msg_queue)), system(system) {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IApplicationProxy::GetCommonStateGetter, "GetCommonStateGetter"},
            {1, &IApplicationProxy::GetSelfController, "GetSelfController"},
            {2, &IApplicationProxy::GetWindowController, "GetWindowController"},
            {3, &IApplicationProxy::GetAudioController, "GetAudioController"},
            {4, &IApplicationProxy::GetDisplayController, "GetDisplayController"},
            {10, nullptr, "GetProcessWindingController"},
            {11, &IApplicationProxy::GetLibraryAppletCreator, "GetLibraryAppletCreator"},
            {20, &IApplicationProxy::GetApplicationFunctions, "GetApplicationFunctions"},
            {1000, &IApplicationProxy::GetDebugFunctions, "GetDebugFunctions"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void GetAudioController(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_AM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<IAudioController>();
    }

    void GetDisplayController(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_AM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<IDisplayController>();
    }

    void GetDebugFunctions(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_AM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<IDebugFunctions>();
    }

    void GetWindowController(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_AM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<IWindowController>();
    }

    void GetSelfController(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_AM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<ISelfController>(nvflinger);
    }

    void GetCommonStateGetter(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_AM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<ICommonStateGetter>(system, msg_queue);
    }

    void GetLibraryAppletCreator(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_AM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<ILibraryAppletCreator>(system.CurrentProcess()->GetTitleID());
    }

    void GetApplicationFunctions(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_AM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<IApplicationFunctions>();
    }

    std::shared_ptr<NVFlinger::NVFlinger> nvflinger;
    std::shared_ptr<AppletMessageQueue> msg_queue;
    Core::System& system;
};

void AppletOE::OpenApplicationProxy(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_AM, "called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<IApplicationProxy>(nvflinger, msg_queue, system);
}

AppletOE::AppletOE(std::shared_ptr<NVFlinger::NVFlinger> nvflinger,
                   std::shared_ptr<AppletMessageQueue> msg_queue, Core::System& system)
    : ServiceFramework("appletOE"), nvflinger(std::move(nvflinger)),
      msg_queue(std::move(msg_queue)), system(system) {
    static const FunctionInfo functions[] = {
        {0, &AppletOE::OpenApplicationProxy, "OpenApplicationProxy"},
    };
    RegisterHandlers(functions);
}

AppletOE::~AppletOE() = default;

const std::shared_ptr<AppletMessageQueue>& AppletOE::GetMessageQueue() const {
    return msg_queue;
}

} // namespace Service::AM
