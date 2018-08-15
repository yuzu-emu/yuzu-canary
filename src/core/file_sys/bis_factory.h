// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include "core/loader/loader.h"
#include "registered_cache.h"

namespace FileSys {

/// File system interface to the Built-In Storage
/// This is currently missing accessors to BIS partitions, but seemed like a good place for the NAND
/// registered caches.
class BISFactory {
public:
    explicit BISFactory(VirtualDir nand_root);

    std::shared_ptr<RegisteredCache> GetSystemNANDContents() const;
    std::shared_ptr<RegisteredCache> GetUserNANDContents() const;

private:
    VirtualDir nand_root;

    std::shared_ptr<RegisteredCache> sysnand_cache;
    std::shared_ptr<RegisteredCache> usrnand_cache;
};

} // namespace FileSys
