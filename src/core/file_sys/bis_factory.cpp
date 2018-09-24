// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <fmt/format.h>
#include "core/file_sys/bis_factory.h"
#include "core/file_sys/registered_cache.h"

namespace FileSys {

BISFactory::BISFactory(VirtualDir nand_root_, VirtualDir load_root_)
    : nand_root(std::move(nand_root_)), load_root(std::move(load_root_)),
      sysnand_cache(std::make_shared<RegisteredCache>(
          GetOrCreateDirectoryRelative(nand_root, "/system/Contents/registered"))),
      usrnand_cache(std::make_shared<RegisteredCache>(
          GetOrCreateDirectoryRelative(nand_root, "/user/Contents/registered"))) {}

BISFactory::~BISFactory() = default;

std::shared_ptr<RegisteredCache> BISFactory::GetSystemNANDContents() const {
    return sysnand_cache;
}

std::shared_ptr<RegisteredCache> BISFactory::GetUserNANDContents() const {
    return usrnand_cache;
}

VirtualDir BISFactory::GetModificationLoadRoot(u64 title_id) const {
    // LayeredFS doesn't work on updates and title id-less homebrew
    if (title_id == 0 || (title_id & 0x800) > 0)
        return nullptr;
    return GetOrCreateDirectoryRelative(load_root, fmt::format("/{:016X}", title_id));
}

} // namespace FileSys
