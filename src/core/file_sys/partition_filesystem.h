// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <string>
#include <vector>
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/swap.h"
#include "core/file_sys/vfs.h"

namespace Loader {
enum class ResultStatus;
}

namespace FileSys {

/**
 * Helper which implements an interface to parse PFS/HFS filesystems.
 * Data can either be loaded from a file path or data with an offset into it.
 */
class PartitionFilesystem : public ReadOnlyVfsDirectory {
public:
    explicit PartitionFilesystem(std::shared_ptr<VfsFile> file);
    Loader::ResultStatus GetStatus() const;

    std::vector<std::shared_ptr<VfsFile>> GetFiles() const override;
    std::vector<std::shared_ptr<VfsDirectory>> GetSubdirectories() const override;
    std::string GetName() const override;
    std::shared_ptr<VfsDirectory> GetParentDirectory() const override;
    void PrintDebugInfo() const;

protected:
    bool ReplaceFileWithSubdirectory(VirtualFile file, VirtualDir dir) override;

private:
    struct Header {
        u32_le magic;
        u32_le num_entries;
        u32_le strtab_size;
        INSERT_PADDING_BYTES(0x4);
    };

    static_assert(sizeof(Header) == 0x10, "PFS/HFS header structure size is wrong");

#pragma pack(push, 1)
    struct FSEntry {
        u64_le offset;
        u64_le size;
        u32_le strtab_offset;
    };

    static_assert(sizeof(FSEntry) == 0x14, "FS entry structure size is wrong");

    struct PFSEntry {
        FSEntry fs_entry;
        INSERT_PADDING_BYTES(0x4);
    };

    static_assert(sizeof(PFSEntry) == 0x18, "PFS entry structure size is wrong");

    struct HFSEntry {
        FSEntry fs_entry;
        u32_le hash_region_size;
        INSERT_PADDING_BYTES(0x8);
        std::array<char, 0x20> hash;
    };

    static_assert(sizeof(HFSEntry) == 0x40, "HFS entry structure size is wrong");

#pragma pack(pop)

    Loader::ResultStatus status;

    Header pfs_header;
    bool is_hfs;
    size_t content_offset;

    std::vector<VirtualFile> pfs_files;
    std::vector<VirtualDir> pfs_dirs;
};

} // namespace FileSys
