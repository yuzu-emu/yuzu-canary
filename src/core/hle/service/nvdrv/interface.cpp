// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cinttypes>
#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/event.h"
#include "core/hle/service/nvdrv/interface.h"
#include "core/hle/service/nvdrv/nvdrv.h"

namespace Service::Nvidia {

void NVDRV::Open(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_NVDRV, "called");

    const auto& buffer = ctx.ReadBuffer();
    std::string device_name(buffer.begin(), buffer.end());

    u32 fd = nvdrv->Open(device_name);
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(fd);
    rb.Push<u32>(0);
}

void NVDRV::Ioctl(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_NVDRV, "called");

    IPC::RequestParser rp{ctx};
    u32 fd = rp.Pop<u32>();
    u32 command = rp.Pop<u32>();

    std::vector<u8> output(ctx.GetWriteBufferSize());

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push(nvdrv->Ioctl(fd, command, ctx.ReadBuffer(), output));

    ctx.WriteBuffer(output);
}

void NVDRV::Close(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_NVDRV, "called");

    IPC::RequestParser rp{ctx};
    u32 fd = rp.Pop<u32>();

    auto result = nvdrv->Close(fd);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result);
}

void NVDRV::Initialize(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_NVDRV, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(0);
}

void NVDRV::QueryEvent(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    u32 fd = rp.Pop<u32>();
    u32 event_id = rp.Pop<u32>();
    LOG_WARNING(Service_NVDRV, "(STUBBED) called, fd={:X}, event_id={:X}", fd, event_id);

    IPC::ResponseBuilder rb{ctx, 3, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushCopyObjects(query_event);
    rb.Push<u32>(0);
}

void NVDRV::SetClientPID(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    pid = rp.Pop<u64>();

    LOG_WARNING(Service_NVDRV, "(STUBBED) called, pid=0x{:X}", pid);
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(0);
}

void NVDRV::FinishInitialize(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_NVDRV, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void NVDRV::GetStatus(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_NVDRV, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void NVDRV::DumpGraphicsMemoryInfo(Kernel::HLERequestContext& ctx) {
    // According to SwitchBrew, this has no inputs and no outputs, so effectively does nothing on
    // retail hardware.
    LOG_DEBUG(Service_NVDRV, "called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

NVDRV::NVDRV(std::shared_ptr<Module> nvdrv, const char* name)
    : ServiceFramework(name), nvdrv(std::move(nvdrv)) {
    static const FunctionInfo functions[] = {
        {0, &NVDRV::Open, "Open"},
        {1, &NVDRV::Ioctl, "Ioctl"},
        {2, &NVDRV::Close, "Close"},
        {3, &NVDRV::Initialize, "Initialize"},
        {4, &NVDRV::QueryEvent, "QueryEvent"},
        {5, nullptr, "MapSharedMem"},
        {6, &NVDRV::GetStatus, "GetStatus"},
        {7, nullptr, "ForceSetClientPID"},
        {8, &NVDRV::SetClientPID, "SetClientPID"},
        {9, &NVDRV::DumpGraphicsMemoryInfo, "DumpGraphicsMemoryInfo"},
        {10, nullptr, "InitializeDevtools"},
        {11, &NVDRV::Ioctl, "Ioctl2"},
        {12, nullptr, "Ioctl3"},
        {13, &NVDRV::FinishInitialize, "FinishInitialize"},
    };
    RegisterHandlers(functions);

    auto& kernel = Core::System::GetInstance().Kernel();
    query_event = Kernel::Event::Create(kernel, Kernel::ResetType::OneShot, "NVDRV::query_event");
}

NVDRV::~NVDRV() = default;

} // namespace Service::Nvidia
