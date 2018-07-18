// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include "core/core.h"
#include "core/file_sys/sdmc_factory.h"

namespace FileSys {

SDMCFactory::SDMCFactory(VirtualDir dir) : dir(std::move(dir)) {}

ResultVal<VirtualDir> SDMCFactory::Open() {
    return MakeResult<VirtualDir>(dir);
}

} // namespace FileSys
