// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include "core/hle/service/service.h"

namespace FileSys {
class FileSystemBackend;
}

namespace Service {
namespace FileSystem {

class FSP_SRV final : public ServiceFramework<FSP_SRV> {
public:
    explicit FSP_SRV();
    ~FSP_SRV() = default;

private:
    void TryLoadRomFS();

    void Initalize(Kernel::HLERequestContext& ctx);
    void MountSdCard(Kernel::HLERequestContext& ctx);
    void GetGlobalAccessLogMode(Kernel::HLERequestContext& ctx);
    void OpenDataStorageByCurrentProcess(Kernel::HLERequestContext& ctx);
    void OpenRomStorage(Kernel::HLERequestContext& ctx);

    std::unique_ptr<FileSys::FileSystemBackend> romfs;
};

} // namespace FileSystem
} // namespace Service
