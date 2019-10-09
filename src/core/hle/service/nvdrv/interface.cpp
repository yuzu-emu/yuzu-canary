// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cinttypes>
#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/readable_event.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/kernel/writable_event.h"
#include "core/hle/service/nvdrv/interface.h"
#include "core/hle/service/nvdrv/nvdata.h"
#include "core/hle/service/nvdrv/nvdrv.h"

namespace Service::Nvidia {

void NVDRV::SignalGPUInterruptSyncpt(const u32 syncpoint_id, const u32 value) {
    nvdrv->SignalSyncpt(syncpoint_id, value);
}

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

void NVDRV::IoctlBase(Kernel::HLERequestContext& ctx, IoctlVersion version) {
    IPC::RequestParser rp{ctx};
    u32 fd = rp.Pop<u32>();
    u32 command = rp.Pop<u32>();

    /// Ioctl 3 has 2 outputs, first in the input params, second is the result
    std::vector<u8> output(ctx.GetWriteBufferSize(0));
    std::vector<u8> output2;
    if (version == IoctlVersion::Version3) {
        output2.resize((ctx.GetWriteBufferSize(1)));
    }

    /// Ioctl2 has 2 inputs. It's used to pass data directly instead of providing a pointer.
    /// KickOfPB uses this
    auto input = ctx.ReadBuffer(0);

    std::vector<u8> input2;
    if (version == IoctlVersion::Version2) {
        input2 = ctx.ReadBuffer(1);
    }

    IoctlCtrl ctrl{};

    u32 result = nvdrv->Ioctl(fd, command, input, input2, output, output2, ctrl, version);

    if (ctrl.must_delay) {
        ctrl.fresh_call = false;
        ctx.SleepClientThread("NVServices::DelayedResponse", ctrl.timeout,
                              [=](Kernel::SharedPtr<Kernel::Thread> thread,
                                  Kernel::HLERequestContext& ctx,
                                  Kernel::ThreadWakeupReason reason) {
                                  IoctlCtrl ctrl2{ctrl};
                                  std::vector<u8> tmp_output = output;
                                  std::vector<u8> tmp_output2 = output2;
                                  u32 result = nvdrv->Ioctl(fd, command, input, input2, tmp_output,
                                                            tmp_output2, ctrl2, version);
                                  ctx.WriteBuffer(tmp_output, 0);
                                  if (version == IoctlVersion::Version3) {
                                      ctx.WriteBuffer(tmp_output2, 1);
                                  }
                                  IPC::ResponseBuilder rb{ctx, 3};
                                  rb.Push(RESULT_SUCCESS);
                                  rb.Push(result);
                              },
                              nvdrv->GetEventWriteable(ctrl.event_id));
    } else {
        ctx.WriteBuffer(output);
        if (version == IoctlVersion::Version3) {
            ctx.WriteBuffer(output2, 1);
        }
    }
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push(result);
}

void NVDRV::Ioctl(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_NVDRV, "called");
    IoctlBase(ctx, IoctlVersion::Version1);
}

void NVDRV::Ioctl2(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_NVDRV, "called");
    IoctlBase(ctx, IoctlVersion::Version2);
}

void NVDRV::Ioctl3(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_NVDRV, "called");
    IoctlBase(ctx, IoctlVersion::Version3);
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
    // TODO(Blinkhawk): Figure the meaning of the flag at bit 16
    u32 event_id = rp.Pop<u32>() & 0x000000FF;
    LOG_WARNING(Service_NVDRV, "(STUBBED) called, fd={:X}, event_id={:X}", fd, event_id);

    IPC::ResponseBuilder rb{ctx, 3, 1};
    rb.Push(RESULT_SUCCESS);
    if (event_id < MaxNvEvents) {
        auto event = nvdrv->GetEvent(event_id);
        event->Clear();
        rb.PushCopyObjects(event);
        rb.Push<u32>(NvResult::Success);
    } else {
        rb.Push<u32>(0);
        rb.Push<u32>(NvResult::BadParameter);
    }
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
        {11, &NVDRV::Ioctl2, "Ioctl2"},
        {12, &NVDRV::Ioctl3, "Ioctl3"},
        {13, &NVDRV::FinishInitialize, "FinishInitialize"},
    };
    RegisterHandlers(functions);
}

NVDRV::~NVDRV() = default;

} // namespace Service::Nvidia
