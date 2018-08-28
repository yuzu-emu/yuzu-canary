// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include "common/common_types.h"
#include "core/file_sys/vfs.h"
#include "core/hle/result.h"

namespace Loader {
class AppLoader;
} // namespace Loader

namespace FileSys {

enum class ContentRecordType : u8;

enum class StorageId : u8 {
    None = 0,
    Host = 1,
    GameCard = 2,
    NandSystem = 3,
    NandUser = 4,
    SdCard = 5,
};

/// File system interface to the RomFS archive
class RomFSFactory {
public:
    explicit RomFSFactory(Loader::AppLoader& app_loader);

    ResultVal<VirtualFile> OpenCurrentProcess();
    ResultVal<VirtualFile> Open(u64 title_id, StorageId storage, ContentRecordType type);

private:
    VirtualFile file;
    bool updatable;
};

} // namespace FileSys
