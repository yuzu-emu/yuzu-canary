// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>

#include "core/file_sys/vfs.h"

namespace FileSys {

class RegisteredCache;

/// File system interface to the Built-In Storage
/// This is currently missing accessors to BIS partitions, but seemed like a good place for the NAND
/// registered caches.
class BISFactory {
public:
    explicit BISFactory(VirtualDir nand_root, VirtualDir load_root);
    ~BISFactory();

    std::shared_ptr<RegisteredCache> GetSystemNANDContents() const;
    std::shared_ptr<RegisteredCache> GetUserNANDContents() const;

    VirtualDir GetModificationLoadRoot(u64 title_id) const;

private:
    VirtualDir nand_root;
    VirtualDir load_root;

    std::shared_ptr<RegisteredCache> sysnand_cache;
    std::shared_ptr<RegisteredCache> usrnand_cache;
};

} // namespace FileSys
