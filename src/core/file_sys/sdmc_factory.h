// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/result.h"

namespace FileSys {

/// File system interface to the SDCard archive
class SDMCFactory {
public:
    explicit SDMCFactory(VirtualDir dir);

    ResultVal<VirtualDir> Open();

private:
    VirtualDir dir;
};

} // namespace FileSys
