// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include "core/hle/service/nvdrv/nvdrv.h"
#include "core/hle/service/service.h"

namespace Kernel {
class WritableEvent;
}

namespace Service::Nvidia {

class NVDRV final : public ServiceFramework<NVDRV> {
public:
    NVDRV(std::shared_ptr<Module> nvdrv, const char* name);
    ~NVDRV() override;

    void SignalGPUInterruptSyncpt(const u32 syncpoint_id, const u32 value);

private:
    void Open(Kernel::HLERequestContext& ctx);
    void Ioctl(Kernel::HLERequestContext& ctx);
    void Ioctl2(Kernel::HLERequestContext& ctx);
    void Ioctl3(Kernel::HLERequestContext& ctx);
    void Close(Kernel::HLERequestContext& ctx);
    void Initialize(Kernel::HLERequestContext& ctx);
    void QueryEvent(Kernel::HLERequestContext& ctx);
    void SetClientPID(Kernel::HLERequestContext& ctx);
    void FinishInitialize(Kernel::HLERequestContext& ctx);
    void GetStatus(Kernel::HLERequestContext& ctx);
    void DumpGraphicsMemoryInfo(Kernel::HLERequestContext& ctx);
    void IoctlBase(Kernel::HLERequestContext& ctx, IoctlVersion version);

    std::shared_ptr<Module> nvdrv;

    u64 pid{};
};

} // namespace Service::Nvidia
