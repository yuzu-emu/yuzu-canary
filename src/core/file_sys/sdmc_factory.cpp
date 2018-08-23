// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/sdmc_factory.h"
#include "core/file_sys/xts_archive.h"

namespace FileSys {

SDMCFactory::SDMCFactory(VirtualDir dir_)
    : dir(std::move(dir_)), contents(std::make_shared<RegisteredCache>(
                                GetOrCreateDirectoryRelative(dir, "/Nintendo/Contents/registered"),
                                [](const VirtualFile& file, const NcaID& id) {
                                    return std::make_shared<NAX>(file, id)->GetDecrypted();
                                })) {}

ResultVal<VirtualDir> SDMCFactory::Open() {
    return MakeResult<VirtualDir>(dir);
}

std::shared_ptr<RegisteredCache> SDMCFactory::GetSDMCContents() const {
    return contents;
}

} // namespace FileSys
