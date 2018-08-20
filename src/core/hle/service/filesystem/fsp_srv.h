// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include "core/hle/service/service.h"

namespace FileSys {
class FileSystemBackend;
}

namespace Service::FileSystem {

class FSP_SRV final : public ServiceFramework<FSP_SRV> {
public:
    explicit FSP_SRV();
    ~FSP_SRV() = default;

private:
    void Initialize(Kernel::HLERequestContext& ctx);
    void MountSdCard(Kernel::HLERequestContext& ctx);
    void CreateSaveData(Kernel::HLERequestContext& ctx);
    void MountSaveData(Kernel::HLERequestContext& ctx);
    void GetGlobalAccessLogMode(Kernel::HLERequestContext& ctx);
    void OpenDataStorageByCurrentProcess(Kernel::HLERequestContext& ctx);
    void OpenDataStorageByDataId(Kernel::HLERequestContext& ctx);
    void OpenRomStorage(Kernel::HLERequestContext& ctx);

    FileSys::VirtualFile romfs;
};

} // namespace Service::FileSystem
