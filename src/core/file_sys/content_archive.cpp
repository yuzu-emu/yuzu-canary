// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <utility>
#include <boost/optional.hpp>
#include "common/logging/log.h"
#include "core/crypto/aes_util.h"
#include "core/crypto/ctr_encryption_layer.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/romfs.h"
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
    NCASectionCryptoType crypto_type;
    INSERT_PADDING_BYTES(3);
};
static_assert(sizeof(NCASectionHeaderBlock) == 0x8, "NCASectionHeaderBlock has incorrect size.");

struct NCASectionRaw {
    NCASectionHeaderBlock header;
    std::array<u8, 0x138> block_data;
    std::array<u8, 0x8> section_ctr;
    INSERT_PADDING_BYTES(0xB8);
};
static_assert(sizeof(NCASectionRaw) == 0x200, "NCASectionRaw has incorrect size.");

struct PFS0Superblock {
    NCASectionHeaderBlock header_block;
    std::array<u8, 0x20> hash;
    u32_le size;
    INSERT_PADDING_BYTES(4);
    u64_le hash_table_offset;
    u64_le hash_table_size;
    u64_le pfs0_header_offset;
    u64_le pfs0_size;
    INSERT_PADDING_BYTES(0x1B0);
};
static_assert(sizeof(PFS0Superblock) == 0x200, "PFS0Superblock has incorrect size.");

struct RomFSSuperblock {
    NCASectionHeaderBlock header_block;
    IVFCHeader ivfc;
    INSERT_PADDING_BYTES(0x118);
};
static_assert(sizeof(RomFSSuperblock) == 0x200, "RomFSSuperblock has incorrect size.");

union NCASectionHeader {
    NCASectionRaw raw;
    PFS0Superblock pfs0;
    RomFSSuperblock romfs;
};
static_assert(sizeof(NCASectionHeader) == 0x200, "NCASectionHeader has incorrect size.");

bool IsValidNCA(const NCAHeader& header) {
    // TODO(DarkLordZach): Add NCA2/NCA0 support.
    return header.magic == Common::MakeMagic('N', 'C', 'A', '3');
}

u8 NCA::GetCryptoRevision() const {
    u8 master_key_id = header.crypto_type;
    if (header.crypto_type_2 > master_key_id)
        master_key_id = header.crypto_type_2;
    if (master_key_id > 0)
        --master_key_id;
    return master_key_id;
}

boost::optional<Core::Crypto::Key128> NCA::GetKeyAreaKey(NCASectionCryptoType type) const {
    const auto master_key_id = GetCryptoRevision();

    if (!keys.HasKey(Core::Crypto::S128KeyType::KeyArea, master_key_id, header.key_index))
        return boost::none;

    std::vector<u8> key_area(header.key_area.begin(), header.key_area.end());
    Core::Crypto::AESCipher<Core::Crypto::Key128> cipher(
        keys.GetKey(Core::Crypto::S128KeyType::KeyArea, master_key_id, header.key_index),
        Core::Crypto::Mode::ECB);
    cipher.Transcode(key_area.data(), key_area.size(), key_area.data(), Core::Crypto::Op::Decrypt);

    Core::Crypto::Key128 out;
    if (type == NCASectionCryptoType::XTS)
        std::copy(key_area.begin(), key_area.begin() + 0x10, out.begin());
    else if (type == NCASectionCryptoType::CTR)
        std::copy(key_area.begin() + 0x20, key_area.begin() + 0x30, out.begin());
    else
        LOG_CRITICAL(Crypto, "Called GetKeyAreaKey on invalid NCASectionCryptoType type={:02X}",
                     static_cast<u8>(type));
    u128 out_128{};
    memcpy(out_128.data(), out.data(), 16);
    LOG_DEBUG(Crypto, "called with crypto_rev={:02X}, kak_index={:02X}, key={:016X}{:016X}",
              master_key_id, header.key_index, out_128[1], out_128[0]);

    return out;
}

boost::optional<Core::Crypto::Key128> NCA::GetTitlekey() {
    const auto master_key_id = GetCryptoRevision();

    u128 rights_id{};
    memcpy(rights_id.data(), header.rights_id.data(), 16);
    if (rights_id == u128{}) {
        status = Loader::ResultStatus::ErrorInvalidRightsID;
        return boost::none;
    }

    auto titlekey = keys.GetKey(Core::Crypto::S128KeyType::Titlekey, rights_id[1], rights_id[0]);
    if (titlekey == Core::Crypto::Key128{}) {
        status = Loader::ResultStatus::ErrorMissingTitlekey;
        return boost::none;
    }

    if (!keys.HasKey(Core::Crypto::S128KeyType::Titlekek, master_key_id)) {
        status = Loader::ResultStatus::ErrorMissingTitlekek;
        return boost::none;
    }

    Core::Crypto::AESCipher<Core::Crypto::Key128> cipher(
        keys.GetKey(Core::Crypto::S128KeyType::Titlekek, master_key_id), Core::Crypto::Mode::ECB);
    cipher.Transcode(titlekey.data(), titlekey.size(), titlekey.data(), Core::Crypto::Op::Decrypt);

    return titlekey;
}

VirtualFile NCA::Decrypt(NCASectionHeader s_header, VirtualFile in, u64 starting_offset) {
    if (!encrypted)
        return in;

    switch (s_header.raw.header.crypto_type) {
    case NCASectionCryptoType::NONE:
        LOG_DEBUG(Crypto, "called with mode=NONE");
        return in;
    case NCASectionCryptoType::CTR:
        LOG_DEBUG(Crypto, "called with mode=CTR, starting_offset={:016X}", starting_offset);
        {
            boost::optional<Core::Crypto::Key128> key = boost::none;
            if (has_rights_id) {
                status = Loader::ResultStatus::Success;
                key = GetTitlekey();
                if (key == boost::none) {
                    if (status == Loader::ResultStatus::Success)
                        status = Loader::ResultStatus::ErrorMissingTitlekey;
                    return nullptr;
                }
            } else {
                key = GetKeyAreaKey(NCASectionCryptoType::CTR);
                if (key == boost::none) {
                    status = Loader::ResultStatus::ErrorMissingKeyAreaKey;
                    return nullptr;
                }
            }

            auto out = std::make_shared<Core::Crypto::CTREncryptionLayer>(
                std::move(in), key.value(), starting_offset);
            std::vector<u8> iv(16);
            for (u8 i = 0; i < 8; ++i)
                iv[i] = s_header.raw.section_ctr[0x8 - i - 1];
            out->SetIV(iv);
            return std::static_pointer_cast<VfsFile>(out);
        }
    case NCASectionCryptoType::XTS:
        // TODO(DarkLordZach): Find a test case for XTS-encrypted NCAs
    default:
        LOG_ERROR(Crypto, "called with unhandled crypto type={:02X}",
                  static_cast<u8>(s_header.raw.header.crypto_type));
        return nullptr;
    }
}

NCA::NCA(VirtualFile file_) : file(std::move(file_)) {
    status = Loader::ResultStatus::Success;

    if (file == nullptr) {
        status = Loader::ResultStatus::ErrorNullFile;
        return;
    }

    if (sizeof(NCAHeader) != file->ReadObject(&header)) {
        LOG_ERROR(Loader, "File reader errored out during header read.");
        status = Loader::ResultStatus::ErrorBadNCAHeader;
        return;
    }

    encrypted = false;

    if (!IsValidNCA(header)) {
        if (header.magic == Common::MakeMagic('N', 'C', 'A', '2')) {
            status = Loader::ResultStatus::ErrorNCA2;
            return;
        }
        if (header.magic == Common::MakeMagic('N', 'C', 'A', '0')) {
            status = Loader::ResultStatus::ErrorNCA0;
            return;
        }

        NCAHeader dec_header{};
        Core::Crypto::AESCipher<Core::Crypto::Key256> cipher(
            keys.GetKey(Core::Crypto::S256KeyType::Header), Core::Crypto::Mode::XTS);
        cipher.XTSTranscode(&header, sizeof(NCAHeader), &dec_header, 0, 0x200,
                            Core::Crypto::Op::Decrypt);
        if (IsValidNCA(dec_header)) {
            header = dec_header;
            encrypted = true;
        } else {
            if (dec_header.magic == Common::MakeMagic('N', 'C', 'A', '2')) {
                status = Loader::ResultStatus::ErrorNCA2;
                return;
            }
            if (dec_header.magic == Common::MakeMagic('N', 'C', 'A', '0')) {
                status = Loader::ResultStatus::ErrorNCA0;
                return;
            }

            if (!keys.HasKey(Core::Crypto::S256KeyType::Header))
                status = Loader::ResultStatus::ErrorMissingHeaderKey;
            else
                status = Loader::ResultStatus::ErrorIncorrectHeaderKey;
            return;
        }
    }

    has_rights_id = std::find_if_not(header.rights_id.begin(), header.rights_id.end(),
                                     [](char c) { return c == '\0'; }) != header.rights_id.end();

    const std::ptrdiff_t number_sections =
        std::count_if(std::begin(header.section_tables), std::end(header.section_tables),
                      [](NCASectionTableEntry entry) { return entry.media_offset > 0; });

    std::vector<NCASectionHeader> sections(number_sections);
    const auto length_sections = SECTION_HEADER_SIZE * number_sections;

    if (encrypted) {
        auto raw = file->ReadBytes(length_sections, SECTION_HEADER_OFFSET);
        Core::Crypto::AESCipher<Core::Crypto::Key256> cipher(
            keys.GetKey(Core::Crypto::S256KeyType::Header), Core::Crypto::Mode::XTS);
        cipher.XTSTranscode(raw.data(), length_sections, sections.data(), 2, SECTION_HEADER_SIZE,
                            Core::Crypto::Op::Decrypt);
    } else {
        file->ReadBytes(sections.data(), length_sections, SECTION_HEADER_OFFSET);
    }

    for (std::ptrdiff_t i = 0; i < number_sections; ++i) {
        auto section = sections[i];

        if (section.raw.header.filesystem_type == NCASectionFilesystemType::ROMFS) {
            const size_t romfs_offset =
                header.section_tables[i].media_offset * MEDIA_OFFSET_MULTIPLIER +
                section.romfs.ivfc.levels[IVFC_MAX_LEVEL - 1].offset;
            const size_t romfs_size = section.romfs.ivfc.levels[IVFC_MAX_LEVEL - 1].size;
            auto dec =
                Decrypt(section, std::make_shared<OffsetVfsFile>(file, romfs_size, romfs_offset),
                        romfs_offset);
            if (dec != nullptr) {
                files.push_back(std::move(dec));
                romfs = files.back();
            } else {
                if (status != Loader::ResultStatus::Success)
                    return;
                if (has_rights_id)
                    status = Loader::ResultStatus::ErrorIncorrectTitlekeyOrTitlekek;
                else
                    status = Loader::ResultStatus::ErrorIncorrectKeyAreaKey;
                return;
            }
        } else if (section.raw.header.filesystem_type == NCASectionFilesystemType::PFS0) {
            u64 offset = (static_cast<u64>(header.section_tables[i].media_offset) *
                          MEDIA_OFFSET_MULTIPLIER) +
                         section.pfs0.pfs0_header_offset;
            u64 size = MEDIA_OFFSET_MULTIPLIER * (header.section_tables[i].media_end_offset -
                                                  header.section_tables[i].media_offset);
            auto dec =
                Decrypt(section, std::make_shared<OffsetVfsFile>(file, size, offset), offset);
            if (dec != nullptr) {
                auto npfs = std::make_shared<PartitionFilesystem>(std::move(dec));

                if (npfs->GetStatus() == Loader::ResultStatus::Success) {
                    dirs.push_back(std::move(npfs));
                    if (IsDirectoryExeFS(dirs.back()))
                        exefs = dirs.back();
                }
            } else {
                if (status != Loader::ResultStatus::Success)
                    return;
                if (has_rights_id)
                    status = Loader::ResultStatus::ErrorIncorrectTitlekeyOrTitlekek;
                else
                    status = Loader::ResultStatus::ErrorIncorrectKeyAreaKey;
                return;
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

VirtualFile NCA::GetBaseFile() const {
    return file;
}

bool NCA::ReplaceFileWithSubdirectory(VirtualFile file, VirtualDir dir) {
    return false;
}
} // namespace FileSys
