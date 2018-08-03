// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <vector>
#include "common/common_types.h"
#include "common/swap.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/vfs.h"
#include "core/loader/loader.h"

namespace FileSys {

enum class GamecardSize : u8 {
    S_1GB = 0xFA,
    S_2GB = 0xF8,
    S_4GB = 0xF0,
    S_8GB = 0xE0,
    S_16GB = 0xE1,
    S_32GB = 0xE2,
};

struct GamecardInfo {
    std::array<u8, 0x70> data;
};
static_assert(sizeof(GamecardInfo) == 0x70, "GamecardInfo has incorrect size.");

struct GamecardHeader {
    std::array<u8, 0x100> signature;
    u32_le magic;
    u32_le secure_area_start;
    u32_le backup_area_start;
    u8 kek_index;
    GamecardSize size;
    u8 header_version;
    u8 flags;
    u64_le package_id;
    u64_le valid_data_end;
    u128 info_iv;
    u64_le hfs_offset;
    u64_le hfs_size;
    std::array<u8, 0x20> hfs_header_hash;
    std::array<u8, 0x20> initial_data_hash;
    u32_le secure_mode_flag;
    u32_le title_key_flag;
    u32_le key_flag;
    u32_le normal_area_end;
    GamecardInfo info;
};
static_assert(sizeof(GamecardHeader) == 0x200, "GamecardHeader has incorrect size.");

enum class XCIPartition : u8 { Update, Normal, Secure, Logo };

class XCI : public ReadOnlyVfsDirectory {
public:
    explicit XCI(VirtualFile file);

    Loader::ResultStatus GetStatus() const;

    u8 GetFormatVersion() const;

    VirtualDir GetPartition(XCIPartition partition) const;
    VirtualDir GetSecurePartition() const;
    VirtualDir GetNormalPartition() const;
    VirtualDir GetUpdatePartition() const;
    VirtualDir GetLogoPartition() const;

    std::shared_ptr<NCA> GetNCAByType(NCAContentType type) const;
    VirtualFile GetNCAFileByType(NCAContentType type) const;

    std::vector<std::shared_ptr<VfsFile>> GetFiles() const override;

    std::vector<std::shared_ptr<VfsDirectory>> GetSubdirectories() const override;

    std::string GetName() const override;

    std::shared_ptr<VfsDirectory> GetParentDirectory() const override;

protected:
    bool ReplaceFileWithSubdirectory(VirtualFile file, VirtualDir dir) override;

private:
    Loader::ResultStatus AddNCAFromPartition(XCIPartition part);

    VirtualFile file;
    GamecardHeader header{};

    Loader::ResultStatus status;

    std::vector<VirtualDir> partitions;
    std::vector<std::shared_ptr<NCA>> ncas;
};
} // namespace FileSys
