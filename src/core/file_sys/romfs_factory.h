// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include "common/common_types.h"
#include "core/hle/result.h"
#include "core/loader/loader.h"

namespace FileSys {

/// File system interface to the RomFS archive
class RomFSFactory {
public:
    explicit RomFSFactory(Loader::AppLoader& app_loader);

    ResultVal<VirtualFile> Open(u64 title_id);

private:
    VirtualFile file;
};

} // namespace FileSys
