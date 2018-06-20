// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <vector>

#include "common/common_funcs.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/swap.h"
#include "core/core.h"
#include "core/file_sys/program_metadata.h"
#include "core/file_sys/romfs_factory.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/resource_limit.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/nca.h"
#include "core/loader/nso.h"
#include "core/memory.h"

namespace Loader {

// Media offsets in headers are stored divided by 512. Mult. by this to get real offset.
constexpr u64 MEDIA_OFFSET_MULTIPLIER = 0x200;

constexpr u64 SECTION_HEADER_SIZE = 0x200;
constexpr u64 SECTION_HEADER_OFFSET = 0x400;

enum class NcaContentType : u8 { Program = 0, Meta = 1, Control = 2, Manual = 3, Data = 4 };

enum class NcaSectionFilesystemType : u8 { PFS0 = 0x2, ROMFS = 0x3 };

struct NcaSectionTableEntry {
    u32_le media_offset;
    u32_le media_end_offset;
    INSERT_PADDING_BYTES(0x8);
};
static_assert(sizeof(NcaSectionTableEntry) == 0x10, "NcaSectionTableEntry has incorrect size.");

struct NcaHeader {
    std::array<u8, 0x100> rsa_signature_1;
    std::array<u8, 0x100> rsa_signature_2;
    u32_le magic;
    u8 is_system;
    NcaContentType content_type;
    u8 crypto_type;
    u8 key_index;
    u64_le size;
    u64_le title_id;
    INSERT_PADDING_BYTES(0x4);
    u32_le sdk_version;
    u8 crypto_type_2;
    INSERT_PADDING_BYTES(15);
    std::array<u8, 0x10> rights_id;
    std::array<NcaSectionTableEntry, 0x4> section_tables;
    std::array<std::array<u8, 0x20>, 0x4> hash_tables;
    std::array<std::array<u8, 0x10>, 0x4> key_area;
    INSERT_PADDING_BYTES(0xC0);
};
static_assert(sizeof(NcaHeader) == 0x400, "NcaHeader has incorrect size.");

struct NcaSectionHeaderBlock {
    INSERT_PADDING_BYTES(3);
    NcaSectionFilesystemType filesystem_type;
    u8 crypto_type;
    INSERT_PADDING_BYTES(3);
};
static_assert(sizeof(NcaSectionHeaderBlock) == 0x8, "NcaSectionHeaderBlock has incorrect size.");

struct Pfs0Superblock {
    NcaSectionHeaderBlock header_block;
    std::array<u8, 0x20> hash;
    u32_le size;
    INSERT_PADDING_BYTES(4);
    u64_le hash_table_offset;
    u64_le hash_table_size;
    u64_le pfs0_header_offset;
    u64_le pfs0_size;
    INSERT_PADDING_BYTES(432);
};
static_assert(sizeof(Pfs0Superblock) == 0x200, "Pfs0Superblock has incorrect size.");

static bool IsValidNca(const NcaHeader& header) {
    return header.magic == Common::MakeMagic('N', 'C', 'A', '2') ||
           header.magic == Common::MakeMagic('N', 'C', 'A', '3');
}

// TODO(DarkLordZach): Add support for encrypted.
class Nca final {
    std::vector<FileSys::PartitionFilesystem> pfs;
    std::vector<u64> pfs_offset;

    u64 romfs_offset = 0;
    u64 romfs_size = 0;

    boost::optional<u8> exefs_id = boost::none;

    FileUtil::IOFile file;
    std::string path;

    u64 GetExeFsFileOffset(const std::string& file_name) const;
    u64 GetExeFsFileSize(const std::string& file_name) const;

public:
    ResultStatus Load(FileUtil::IOFile&& file, std::string path);

    FileSys::PartitionFilesystem GetPfs(u8 id) const;

    u64 GetRomFsOffset() const;
    u64 GetRomFsSize() const;

    std::vector<u8> GetExeFsFile(const std::string& file_name);
};

static bool IsPfsExeFs(const FileSys::PartitionFilesystem& pfs) {
    // According to switchbrew, an exefs must only contain these two files:
    return pfs.GetFileSize("main") > 0 && pfs.GetFileSize("main.npdm") > 0;
}

ResultStatus Nca::Load(FileUtil::IOFile&& in_file, std::string in_path) {
    file = std::move(in_file);
    path = in_path;
    file.Seek(0, SEEK_SET);
    std::array<u8, sizeof(NcaHeader)> header_array{};
    if (sizeof(NcaHeader) != file.ReadBytes(header_array.data(), sizeof(NcaHeader)))
        NGLOG_CRITICAL(Loader, "File reader errored out during header read.");

    NcaHeader header{};
    std::memcpy(&header, header_array.data(), sizeof(NcaHeader));
    if (!IsValidNca(header))
        return ResultStatus::ErrorInvalidFormat;

    int number_sections =
        std::count_if(std::begin(header.section_tables), std::end(header.section_tables),
                      [](NcaSectionTableEntry entry) { return entry.media_offset > 0; });

    for (int i = 0; i < number_sections; ++i) {
        // Seek to beginning of this section.
        file.Seek(SECTION_HEADER_OFFSET + i * SECTION_HEADER_SIZE, SEEK_SET);
        std::array<u8, sizeof(NcaSectionHeaderBlock)> array{};
        if (sizeof(NcaSectionHeaderBlock) !=
            file.ReadBytes(array.data(), sizeof(NcaSectionHeaderBlock)))
            NGLOG_CRITICAL(Loader, "File reader errored out during header read.");

        NcaSectionHeaderBlock block{};
        std::memcpy(&block, array.data(), sizeof(NcaSectionHeaderBlock));

        if (block.filesystem_type == NcaSectionFilesystemType::ROMFS) {
            romfs_offset = header.section_tables[i].media_offset * MEDIA_OFFSET_MULTIPLIER;
            romfs_size =
                header.section_tables[i].media_end_offset * MEDIA_OFFSET_MULTIPLIER - romfs_offset;
        } else if (block.filesystem_type == NcaSectionFilesystemType::PFS0) {
            Pfs0Superblock sb{};
            // Seek back to beginning of this section.
            file.Seek(SECTION_HEADER_OFFSET + i * SECTION_HEADER_SIZE, SEEK_SET);
            if (sizeof(Pfs0Superblock) != file.ReadBytes(&sb, sizeof(Pfs0Superblock)))
                NGLOG_CRITICAL(Loader, "File reader errored out during header read.");

            u64 offset = (static_cast<u64>(header.section_tables[i].media_offset) *
                          MEDIA_OFFSET_MULTIPLIER) +
                         sb.pfs0_header_offset;
            FileSys::PartitionFilesystem npfs{};
            ResultStatus status = npfs.Load(path, offset);

            if (status == ResultStatus::Success) {
                pfs.emplace_back(std::move(npfs));
                pfs_offset.emplace_back(offset);
            }
        }
    }

    for (size_t i = 0; i < pfs.size(); ++i) {
        if (IsPfsExeFs(pfs[i]))
            exefs_id = i;
    }

    return ResultStatus::Success;
}

FileSys::PartitionFilesystem Nca::GetPfs(u8 id) const {
    return pfs[id];
}

u64 Nca::GetExeFsFileOffset(const std::string& file_name) const {
    if (exefs_id == boost::none)
        return 0;
    return pfs[*exefs_id].GetFileOffset(file_name) + pfs_offset[*exefs_id];
}

u64 Nca::GetExeFsFileSize(const std::string& file_name) const {
    if (exefs_id == boost::none)
        return 0;
    return pfs[*exefs_id].GetFileSize(file_name);
}

u64 Nca::GetRomFsOffset() const {
    return romfs_offset;
}

u64 Nca::GetRomFsSize() const {
    return romfs_size;
}

std::vector<u8> Nca::GetExeFsFile(const std::string& file_name) {
    std::vector<u8> out(GetExeFsFileSize(file_name));
    file.Seek(GetExeFsFileOffset(file_name), SEEK_SET);
    file.ReadBytes(out.data(), GetExeFsFileSize(file_name));
    return out;
}

AppLoader_NCA::AppLoader_NCA(FileUtil::IOFile&& file, std::string filepath)
    : AppLoader(std::move(file)), filepath(std::move(filepath)) {}

FileType AppLoader_NCA::IdentifyType(FileUtil::IOFile& file, const std::string&) {
    file.Seek(0, SEEK_SET);
    std::array<u8, 0x400> header_enc_array{};
    if (0x400 != file.ReadBytes(header_enc_array.data(), 0x400))
        return FileType::Error;

    // TODO(DarkLordZach): Assuming everything is decrypted. Add crypto support.
    NcaHeader header{};
    std::memcpy(&header, header_enc_array.data(), sizeof(NcaHeader));

    if (IsValidNca(header) && header.content_type == NcaContentType::Program)
        return FileType::NCA;

    return FileType::Error;
}

ResultStatus AppLoader_NCA::Load(Kernel::SharedPtr<Kernel::Process>& process) {
    if (is_loaded) {
        return ResultStatus::ErrorAlreadyLoaded;
    }
    if (!file.IsOpen()) {
        return ResultStatus::Error;
    }

    nca = std::make_unique<Nca>();
    ResultStatus result = nca->Load(std::move(file), filepath);
    if (result != ResultStatus::Success) {
        return result;
    }

    result = metadata.Load(nca->GetExeFsFile("main.npdm"));
    if (result != ResultStatus::Success) {
        return result;
    }
    metadata.Print();

    const FileSys::ProgramAddressSpaceType arch_bits{metadata.GetAddressSpaceType()};
    if (arch_bits == FileSys::ProgramAddressSpaceType::Is32Bit) {
        return ResultStatus::ErrorUnsupportedArch;
    }

    VAddr next_load_addr{Memory::PROCESS_IMAGE_VADDR};
    for (const auto& module : {"rtld", "main", "subsdk0", "subsdk1", "subsdk2", "subsdk3",
                               "subsdk4", "subsdk5", "subsdk6", "subsdk7", "sdk"}) {
        const VAddr load_addr = next_load_addr;
        next_load_addr = AppLoader_NSO::LoadModule(module, nca->GetExeFsFile(module), load_addr);
        if (next_load_addr) {
            NGLOG_DEBUG(Loader, "loaded module {} @ 0x{:X}", module, load_addr);
        } else {
            next_load_addr = load_addr;
        }
    }

    process->program_id = metadata.GetTitleID();
    process->svc_access_mask.set();
    process->address_mappings = default_address_mappings;
    process->resource_limit =
        Kernel::ResourceLimit::GetForCategory(Kernel::ResourceLimitCategory::APPLICATION);
    process->Run(Memory::PROCESS_IMAGE_VADDR, metadata.GetMainThreadPriority(),
                 metadata.GetMainThreadStackSize());

    if (nca->GetRomFsSize() > 0)
        Service::FileSystem::RegisterFileSystem(std::make_unique<FileSys::RomFS_Factory>(*this),
                                                Service::FileSystem::Type::RomFS);

    is_loaded = true;
    return ResultStatus::Success;
}

ResultStatus AppLoader_NCA::ReadRomFS(std::shared_ptr<FileUtil::IOFile>& romfs_file, u64& offset,
                                      u64& size) {
    if (nca->GetRomFsSize() == 0) {
        NGLOG_DEBUG(Loader, "No RomFS available");
        return ResultStatus::ErrorNotUsed;
    }

    romfs_file = std::make_shared<FileUtil::IOFile>(filepath, "rb");

    offset = nca->GetRomFsOffset();
    size = nca->GetRomFsSize();

    NGLOG_DEBUG(Loader, "RomFS offset:           0x{:016X}", offset);
    NGLOG_DEBUG(Loader, "RomFS size:             0x{:016X}", size);

    return ResultStatus::Success;
}

AppLoader_NCA::~AppLoader_NCA() = default;

} // namespace Loader
