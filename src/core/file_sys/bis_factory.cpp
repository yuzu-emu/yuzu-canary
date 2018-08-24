// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/file_sys/bis_factory.h"

namespace FileSys {

BISFactory::BISFactory(VirtualDir nand_root_)
    : nand_root(std::move(nand_root_)),
      sysnand_cache(std::make_shared<RegisteredCache>(
          GetOrCreateDirectoryRelative(nand_root, "/system/Contents/registered"))),
      usrnand_cache(std::make_shared<RegisteredCache>(
          GetOrCreateDirectoryRelative(nand_root, "/user/Contents/registered"))) {}

std::shared_ptr<RegisteredCache> BISFactory::GetSystemNANDContents() const {
    return sysnand_cache;
}

std::shared_ptr<RegisteredCache> BISFactory::GetUserNANDContents() const {
    return usrnand_cache;
}

} // namespace FileSys
