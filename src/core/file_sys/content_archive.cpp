// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/vfs_offset.h"
#include "core/loader/loader.h"

namespace FileSys {

// Media offsets in headers are stored divided by 512. Mult. by this to get real offset.
constexpr u64 MEDIA_OFFSET_MULTIPLIER = 0x200;

constexpr u64 SECTION_HEADER_SIZE = 0x200;
constexpr u64 SECTION_HEADER_OFFSET = 0x400;

constexpr u32 IVFC_MAX_LEVEL = 6;

enum class NCASectionFilesystemType : u8 {
    PFS0 = 0x2,
    ROMFS = 0x3,
};

struct NCASectionHeaderBlock {
    INSERT_PADDING_BYTES(3);
    NCASectionFilesystemType filesystem_type;
    u8 crypto_type;
    INSERT_PADDING_BYTES(3);
};
static_assert(sizeof(NCASectionHeaderBlock) == 0x8, "NCASectionHeaderBlock has incorrect size.");

struct PFS0Superblock {
    NCASectionHeaderBlock header_block;
    std::array<u8, 0x20> hash;
    u32_le size;
    INSERT_PADDING_BYTES(4);
    u64_le hash_table_offset;
    u64_le hash_table_size;
    u64_le pfs0_header_offset;
    u64_le pfs0_size;
    INSERT_PADDING_BYTES(432);
};
static_assert(sizeof(PFS0Superblock) == 0x200, "PFS0Superblock has incorrect size.");

struct IVFCLevel {
    u64_le offset;
    u64_le size;
    u32_le block_size;
    u32_le reserved;
};
static_assert(sizeof(IVFCLevel) == 0x18, "IVFCLevel has incorrect size.");

struct RomFSSuperblock {
    NCASectionHeaderBlock header_block;
    u32_le magic;
    u32_le magic_number;
    INSERT_PADDING_BYTES(8);
    std::array<IVFCLevel, 6> levels;
    INSERT_PADDING_BYTES(64);
};
static_assert(sizeof(RomFSSuperblock) == 0xE8, "RomFSSuperblock has incorrect size.");

NCA::NCA(VirtualFile file_) : file(file_) {
    if (sizeof(NCAHeader) != file->ReadObject(&header))
        LOG_CRITICAL(Loader, "File reader errored out during header read.");

    if (!IsValidNCA(header)) {
        status = Loader::ResultStatus::ErrorInvalidFormat;
        return;
    }

    std::ptrdiff_t number_sections =
        std::count_if(std::begin(header.section_tables), std::end(header.section_tables),
                      [](NCASectionTableEntry entry) { return entry.media_offset > 0; });

    for (std::ptrdiff_t i = 0; i < number_sections; ++i) {
        // Seek to beginning of this section.
        NCASectionHeaderBlock block{};
        if (sizeof(NCASectionHeaderBlock) !=
            file->ReadObject(&block, SECTION_HEADER_OFFSET + i * SECTION_HEADER_SIZE))
            LOG_CRITICAL(Loader, "File reader errored out during header read.");

        if (block.filesystem_type == NCASectionFilesystemType::ROMFS) {
            RomFSSuperblock sb{};
            if (sizeof(RomFSSuperblock) !=
                file->ReadObject(&sb, SECTION_HEADER_OFFSET + i * SECTION_HEADER_SIZE))
                LOG_CRITICAL(Loader, "File reader errored out during header read.");

            const size_t romfs_offset =
                header.section_tables[i].media_offset * MEDIA_OFFSET_MULTIPLIER +
                sb.levels[IVFC_MAX_LEVEL - 1].offset;
            const size_t romfs_size = sb.levels[IVFC_MAX_LEVEL - 1].size;
            files.emplace_back(std::make_shared<OffsetVfsFile>(file, romfs_size, romfs_offset));
            romfs = files.back();
        } else if (block.filesystem_type == NCASectionFilesystemType::PFS0) {
            PFS0Superblock sb{};
            // Seek back to beginning of this section.
            if (sizeof(PFS0Superblock) !=
                file->ReadObject(&sb, SECTION_HEADER_OFFSET + i * SECTION_HEADER_SIZE))
                LOG_CRITICAL(Loader, "File reader errored out during header read.");

            u64 offset = (static_cast<u64>(header.section_tables[i].media_offset) *
                          MEDIA_OFFSET_MULTIPLIER) +
                         sb.pfs0_header_offset;
            u64 size = MEDIA_OFFSET_MULTIPLIER * (header.section_tables[i].media_end_offset -
                                                  header.section_tables[i].media_offset);
            auto npfs = std::make_shared<PartitionFilesystem>(
                std::make_shared<OffsetVfsFile>(file, size, offset));

            if (npfs->GetStatus() == Loader::ResultStatus::Success) {
                dirs.emplace_back(npfs);
                if (IsDirectoryExeFS(dirs.back()))
                    exefs = dirs.back();
            }
        }
    }

    status = Loader::ResultStatus::Success;
}

Loader::ResultStatus NCA::GetStatus() const {
    return status;
}

std::vector<std::shared_ptr<VfsFile>> NCA::GetFiles() const {
    if (status != Loader::ResultStatus::Success)
        return {};
    return files;
}

std::vector<std::shared_ptr<VfsDirectory>> NCA::GetSubdirectories() const {
    if (status != Loader::ResultStatus::Success)
        return {};
    return dirs;
}

std::string NCA::GetName() const {
    return file->GetName();
}

std::shared_ptr<VfsDirectory> NCA::GetParentDirectory() const {
    return file->GetContainingDirectory();
}

NCAContentType NCA::GetType() const {
    return header.content_type;
}

u64 NCA::GetTitleId() const {
    if (status != Loader::ResultStatus::Success)
        return {};
    return header.title_id;
}

VirtualFile NCA::GetRomFS() const {
    return romfs;
}

VirtualDir NCA::GetExeFS() const {
    return exefs;
}

bool NCA::ReplaceFileWithSubdirectory(VirtualFile file, VirtualDir dir) {
    return false;
}
} // namespace FileSys
