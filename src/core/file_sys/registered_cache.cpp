// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <regex>
#include <mbedtls/sha256.h>
#include "common/assert.h"
#include "common/file_util.h"
#include "common/hex_util.h"
#include "common/logging/log.h"
#include "core/crypto/key_manager.h"
#include "core/file_sys/card_image.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/submission_package.h"
#include "core/file_sys/vfs_concat.h"
#include "core/loader/loader.h"

namespace FileSys {

// The size of blocks to use when vfs raw copying into nand.
constexpr size_t VFS_RC_LARGE_COPY_BLOCK = 0x400000;

std::string ContentProviderEntry::DebugInfo() const {
    return fmt::format("title_id={:016X}, content_type={:02X}", title_id, static_cast<u8>(type));
}

bool operator<(const ContentProviderEntry& lhs, const ContentProviderEntry& rhs) {
    return (lhs.title_id < rhs.title_id) || (lhs.title_id == rhs.title_id && lhs.type < rhs.type);
}

bool operator==(const ContentProviderEntry& lhs, const ContentProviderEntry& rhs) {
    return std::tie(lhs.title_id, lhs.type) == std::tie(rhs.title_id, rhs.type);
}

bool operator!=(const ContentProviderEntry& lhs, const ContentProviderEntry& rhs) {
    return !operator==(lhs, rhs);
}

static bool FollowsTwoDigitDirFormat(std::string_view name) {
    static const std::regex two_digit_regex("000000[0-9A-F]{2}", std::regex_constants::ECMAScript |
                                                                     std::regex_constants::icase);
    return std::regex_match(name.begin(), name.end(), two_digit_regex);
}

static bool FollowsNcaIdFormat(std::string_view name) {
    static const std::regex nca_id_regex("[0-9A-F]{32}\\.nca", std::regex_constants::ECMAScript |
                                                                   std::regex_constants::icase);
    return name.size() == 36 && std::regex_match(name.begin(), name.end(), nca_id_regex);
}

static std::string GetRelativePathFromNcaID(const std::array<u8, 16>& nca_id, bool second_hex_upper,
                                            bool within_two_digit) {
    if (!within_two_digit)
        return fmt::format("/{}.nca", Common::HexArrayToString(nca_id, second_hex_upper));

    Core::Crypto::SHA256Hash hash{};
    mbedtls_sha256(nca_id.data(), nca_id.size(), hash.data(), 0);
    return fmt::format("/000000{:02X}/{}.nca", hash[0],
                       Common::HexArrayToString(nca_id, second_hex_upper));
}

static std::string GetCNMTName(TitleType type, u64 title_id) {
    constexpr std::array<const char*, 9> TITLE_TYPE_NAMES{
        "SystemProgram",
        "SystemData",
        "SystemUpdate",
        "BootImagePackage",
        "BootImagePackageSafe",
        "Application",
        "Patch",
        "AddOnContent",
        "" ///< Currently unknown 'DeltaTitle'
    };

    auto index = static_cast<std::size_t>(type);
    // If the index is after the jump in TitleType, subtract it out.
    if (index >= static_cast<std::size_t>(TitleType::Application)) {
        index -= static_cast<std::size_t>(TitleType::Application) -
                 static_cast<std::size_t>(TitleType::FirmwarePackageB);
    }
    return fmt::format("{}_{:016x}.cnmt", TITLE_TYPE_NAMES[index], title_id);
}

ContentRecordType GetCRTypeFromNCAType(NCAContentType type) {
    switch (type) {
    case NCAContentType::Program:
        // TODO(DarkLordZach): Differentiate between Program and Patch
        return ContentRecordType::Program;
    case NCAContentType::Meta:
        return ContentRecordType::Meta;
    case NCAContentType::Control:
        return ContentRecordType::Control;
    case NCAContentType::Data:
    case NCAContentType::PublicData:
        return ContentRecordType::Data;
    case NCAContentType::Manual:
        // TODO(DarkLordZach): Peek at NCA contents to differentiate Manual and Legal.
        return ContentRecordType::Manual;
    default:
        UNREACHABLE_MSG("Invalid NCAContentType={:02X}", static_cast<u8>(type));
    }
}

ContentProvider::~ContentProvider() = default;

bool ContentProvider::HasEntry(ContentProviderEntry entry) const {
    return HasEntry(entry.title_id, entry.type);
}

VirtualFile ContentProvider::GetEntryUnparsed(ContentProviderEntry entry) const {
    return GetEntryUnparsed(entry.title_id, entry.type);
}

VirtualFile ContentProvider::GetEntryRaw(ContentProviderEntry entry) const {
    return GetEntryRaw(entry.title_id, entry.type);
}

std::unique_ptr<NCA> ContentProvider::GetEntry(ContentProviderEntry entry) const {
    return GetEntry(entry.title_id, entry.type);
}

std::vector<ContentProviderEntry> ContentProvider::ListEntries() const {
    return ListEntriesFilter(std::nullopt, std::nullopt, std::nullopt);
}

VirtualFile RegisteredCache::OpenFileOrDirectoryConcat(const VirtualDir& dir,
                                                       std::string_view path) const {
    const auto file = dir->GetFileRelative(path);
    if (file != nullptr) {
        return file;
    }

    const auto nca_dir = dir->GetDirectoryRelative(path);
    if (nca_dir == nullptr) {
        return nullptr;
    }

    const auto files = nca_dir->GetFiles();
    if (files.size() == 1 && files[0]->GetName() == "00") {
        return files[0];
    }

    std::vector<VirtualFile> concat;
    // Since the files are a two-digit hex number, max is FF.
    for (std::size_t i = 0; i < 0x100; ++i) {
        auto next = nca_dir->GetFile(fmt::format("{:02X}", i));
        if (next != nullptr) {
            concat.push_back(std::move(next));
        } else {
            next = nca_dir->GetFile(fmt::format("{:02x}", i));
            if (next != nullptr) {
                concat.push_back(std::move(next));
            } else {
                break;
            }
        }
    }

    if (concat.empty()) {
        return nullptr;
    }

    return ConcatenatedVfsFile::MakeConcatenatedFile(concat, concat.front()->GetName());
}

VirtualFile RegisteredCache::GetFileAtID(NcaID id) const {
    VirtualFile file;
    // Try all four modes of file storage:
    // (bit 1 = uppercase/lower, bit 0 = within a two-digit dir)
    // 00: /000000**/{:032X}.nca
    // 01: /{:032X}.nca
    // 10: /000000**/{:032x}.nca
    // 11: /{:032x}.nca
    for (u8 i = 0; i < 4; ++i) {
        const auto path = GetRelativePathFromNcaID(id, (i & 0b10) == 0, (i & 0b01) == 0);
        file = OpenFileOrDirectoryConcat(dir, path);
        if (file != nullptr)
            return file;
    }
    return file;
}

static std::optional<NcaID> CheckMapForContentRecord(const std::map<u64, CNMT>& map, u64 title_id,
                                                     ContentRecordType type) {
    if (map.find(title_id) == map.end())
        return {};

    const auto& cnmt = map.at(title_id);

    const auto iter = std::find_if(cnmt.GetContentRecords().begin(), cnmt.GetContentRecords().end(),
                                   [type](const ContentRecord& rec) { return rec.type == type; });
    if (iter == cnmt.GetContentRecords().end())
        return {};

    return std::make_optional(iter->nca_id);
}

std::optional<NcaID> RegisteredCache::GetNcaIDFromMetadata(u64 title_id,
                                                           ContentRecordType type) const {
    if (type == ContentRecordType::Meta && meta_id.find(title_id) != meta_id.end())
        return meta_id.at(title_id);

    const auto res1 = CheckMapForContentRecord(yuzu_meta, title_id, type);
    if (res1)
        return res1;
    return CheckMapForContentRecord(meta, title_id, type);
}

std::vector<NcaID> RegisteredCache::AccumulateFiles() const {
    std::vector<NcaID> ids;
    for (const auto& d2_dir : dir->GetSubdirectories()) {
        if (FollowsNcaIdFormat(d2_dir->GetName())) {
            ids.push_back(Common::HexStringToArray<0x10, true>(d2_dir->GetName().substr(0, 0x20)));
            continue;
        }

        if (!FollowsTwoDigitDirFormat(d2_dir->GetName()))
            continue;

        for (const auto& nca_dir : d2_dir->GetSubdirectories()) {
            if (!FollowsNcaIdFormat(nca_dir->GetName()))
                continue;

            ids.push_back(Common::HexStringToArray<0x10, true>(nca_dir->GetName().substr(0, 0x20)));
        }

        for (const auto& nca_file : d2_dir->GetFiles()) {
            if (!FollowsNcaIdFormat(nca_file->GetName()))
                continue;

            ids.push_back(
                Common::HexStringToArray<0x10, true>(nca_file->GetName().substr(0, 0x20)));
        }
    }

    for (const auto& d2_file : dir->GetFiles()) {
        if (FollowsNcaIdFormat(d2_file->GetName()))
            ids.push_back(Common::HexStringToArray<0x10, true>(d2_file->GetName().substr(0, 0x20)));
    }
    return ids;
}

void RegisteredCache::ProcessFiles(const std::vector<NcaID>& ids) {
    for (const auto& id : ids) {
        const auto file = GetFileAtID(id);

        if (file == nullptr)
            continue;
        const auto nca = std::make_shared<NCA>(parser(file, id), nullptr, 0, keys);
        if (nca->GetStatus() != Loader::ResultStatus::Success ||
            nca->GetType() != NCAContentType::Meta) {
            continue;
        }

        const auto section0 = nca->GetSubdirectories()[0];

        for (const auto& section0_file : section0->GetFiles()) {
            if (section0_file->GetExtension() != "cnmt")
                continue;

            meta.insert_or_assign(nca->GetTitleId(), CNMT(section0_file));
            meta_id.insert_or_assign(nca->GetTitleId(), id);
            break;
        }
    }
}

void RegisteredCache::AccumulateYuzuMeta() {
    const auto dir = this->dir->GetSubdirectory("yuzu_meta");
    if (dir == nullptr)
        return;

    for (const auto& file : dir->GetFiles()) {
        if (file->GetExtension() != "cnmt")
            continue;

        CNMT cnmt(file);
        yuzu_meta.insert_or_assign(cnmt.GetTitleID(), std::move(cnmt));
    }
}

void RegisteredCache::Refresh() {
    if (dir == nullptr)
        return;
    const auto ids = AccumulateFiles();
    ProcessFiles(ids);
    AccumulateYuzuMeta();
}

RegisteredCache::RegisteredCache(VirtualDir dir_, ContentProviderParsingFunction parsing_function)
    : dir(std::move(dir_)), parser(std::move(parsing_function)) {
    Refresh();
}

RegisteredCache::~RegisteredCache() = default;

bool RegisteredCache::HasEntry(u64 title_id, ContentRecordType type) const {
    return GetEntryRaw(title_id, type) != nullptr;
}

VirtualFile RegisteredCache::GetEntryUnparsed(u64 title_id, ContentRecordType type) const {
    const auto id = GetNcaIDFromMetadata(title_id, type);
    return id ? GetFileAtID(*id) : nullptr;
}

std::optional<u32> RegisteredCache::GetEntryVersion(u64 title_id) const {
    const auto meta_iter = meta.find(title_id);
    if (meta_iter != meta.end())
        return meta_iter->second.GetTitleVersion();

    const auto yuzu_meta_iter = yuzu_meta.find(title_id);
    if (yuzu_meta_iter != yuzu_meta.end())
        return yuzu_meta_iter->second.GetTitleVersion();

    return {};
}

VirtualFile RegisteredCache::GetEntryRaw(u64 title_id, ContentRecordType type) const {
    const auto id = GetNcaIDFromMetadata(title_id, type);
    return id ? parser(GetFileAtID(*id), *id) : nullptr;
}

std::unique_ptr<NCA> RegisteredCache::GetEntry(u64 title_id, ContentRecordType type) const {
    const auto raw = GetEntryRaw(title_id, type);
    if (raw == nullptr)
        return nullptr;
    return std::make_unique<NCA>(raw, nullptr, 0, keys);
}

template <typename T>
void RegisteredCache::IterateAllMetadata(
    std::vector<T>& out, std::function<T(const CNMT&, const ContentRecord&)> proc,
    std::function<bool(const CNMT&, const ContentRecord&)> filter) const {
    for (const auto& kv : meta) {
        const auto& cnmt = kv.second;
        if (filter(cnmt, EMPTY_META_CONTENT_RECORD))
            out.push_back(proc(cnmt, EMPTY_META_CONTENT_RECORD));
        for (const auto& rec : cnmt.GetContentRecords()) {
            if (GetFileAtID(rec.nca_id) != nullptr && filter(cnmt, rec)) {
                out.push_back(proc(cnmt, rec));
            }
        }
    }
    for (const auto& kv : yuzu_meta) {
        const auto& cnmt = kv.second;
        for (const auto& rec : cnmt.GetContentRecords()) {
            if (GetFileAtID(rec.nca_id) != nullptr && filter(cnmt, rec)) {
                out.push_back(proc(cnmt, rec));
            }
        }
    }
}

std::vector<ContentProviderEntry> RegisteredCache::ListEntriesFilter(
    std::optional<TitleType> title_type, std::optional<ContentRecordType> record_type,
    std::optional<u64> title_id) const {
    std::vector<ContentProviderEntry> out;
    IterateAllMetadata<ContentProviderEntry>(
        out,
        [](const CNMT& c, const ContentRecord& r) {
            return ContentProviderEntry{c.GetTitleID(), r.type};
        },
        [&title_type, &record_type, &title_id](const CNMT& c, const ContentRecord& r) {
            if (title_type && *title_type != c.GetType())
                return false;
            if (record_type && *record_type != r.type)
                return false;
            if (title_id && *title_id != c.GetTitleID())
                return false;
            return true;
        });
    return out;
}

static std::shared_ptr<NCA> GetNCAFromNSPForID(const NSP& nsp, const NcaID& id) {
    const auto file = nsp.GetFile(fmt::format("{}.nca", Common::HexArrayToString(id, false)));
    if (file == nullptr)
        return nullptr;
    return std::make_shared<NCA>(file);
}

InstallResult RegisteredCache::InstallEntry(const XCI& xci, bool overwrite_if_exists,
                                            const VfsCopyFunction& copy) {
    return InstallEntry(*xci.GetSecurePartitionNSP(), overwrite_if_exists, copy);
}

InstallResult RegisteredCache::InstallEntry(const NSP& nsp, bool overwrite_if_exists,
                                            const VfsCopyFunction& copy) {
    const auto ncas = nsp.GetNCAsCollapsed();
    const auto meta_iter = std::find_if(ncas.begin(), ncas.end(), [](const auto& nca) {
        return nca->GetType() == NCAContentType::Meta;
    });

    if (meta_iter == ncas.end()) {
        LOG_ERROR(Loader, "The XCI you are attempting to install does not have a metadata NCA and "
                          "is therefore malformed. Double check your encryption keys.");
        return InstallResult::ErrorMetaFailed;
    }

    // Install Metadata File
    const auto meta_id_raw = (*meta_iter)->GetName().substr(0, 32);
    const auto meta_id = Common::HexStringToArray<16>(meta_id_raw);

    const auto res = RawInstallNCA(**meta_iter, copy, overwrite_if_exists, meta_id);
    if (res != InstallResult::Success)
        return res;

    // Install all the other NCAs
    const auto section0 = (*meta_iter)->GetSubdirectories()[0];
    const auto cnmt_file = section0->GetFiles()[0];
    const CNMT cnmt(cnmt_file);
    for (const auto& record : cnmt.GetContentRecords()) {
        const auto nca = GetNCAFromNSPForID(nsp, record.nca_id);
        if (nca == nullptr)
            return InstallResult::ErrorCopyFailed;
        const auto res2 = RawInstallNCA(*nca, copy, overwrite_if_exists, record.nca_id);
        if (res2 != InstallResult::Success)
            return res2;
    }

    Refresh();
    return InstallResult::Success;
}

InstallResult RegisteredCache::InstallEntry(const NCA& nca, TitleType type,
                                            bool overwrite_if_exists, const VfsCopyFunction& copy) {
    CNMTHeader header{
        nca.GetTitleId(), ///< Title ID
        0,                ///< Ignore/Default title version
        type,             ///< Type
        {},               ///< Padding
        0x10,             ///< Default table offset
        1,                ///< 1 Content Entry
        0,                ///< No Meta Entries
        {},               ///< Padding
    };
    OptionalHeader opt_header{0, 0};
    ContentRecord c_rec{{}, {}, {}, GetCRTypeFromNCAType(nca.GetType()), {}};
    const auto& data = nca.GetBaseFile()->ReadBytes(0x100000);
    mbedtls_sha256(data.data(), data.size(), c_rec.hash.data(), 0);
    memcpy(&c_rec.nca_id, &c_rec.hash, 16);
    const CNMT new_cnmt(header, opt_header, {c_rec}, {});
    if (!RawInstallYuzuMeta(new_cnmt))
        return InstallResult::ErrorMetaFailed;
    return RawInstallNCA(nca, copy, overwrite_if_exists, c_rec.nca_id);
}

InstallResult RegisteredCache::RawInstallNCA(const NCA& nca, const VfsCopyFunction& copy,
                                             bool overwrite_if_exists,
                                             std::optional<NcaID> override_id) {
    const auto in = nca.GetBaseFile();
    Core::Crypto::SHA256Hash hash{};

    // Calculate NcaID
    // NOTE: Because computing the SHA256 of an entire NCA is quite expensive (especially if the
    // game is massive), we're going to cheat and only hash the first MB of the NCA.
    // Also, for XCIs the NcaID matters, so if the override id isn't none, use that.
    NcaID id{};
    if (override_id) {
        id = *override_id;
    } else {
        const auto& data = in->ReadBytes(0x100000);
        mbedtls_sha256(data.data(), data.size(), hash.data(), 0);
        memcpy(id.data(), hash.data(), 16);
    }

    std::string path = GetRelativePathFromNcaID(id, false, true);

    if (GetFileAtID(id) != nullptr && !overwrite_if_exists) {
        LOG_WARNING(Loader, "Attempting to overwrite existing NCA. Skipping...");
        return InstallResult::ErrorAlreadyExists;
    }

    if (GetFileAtID(id) != nullptr) {
        LOG_WARNING(Loader, "Overwriting existing NCA...");
        VirtualDir c_dir;
        { c_dir = dir->GetFileRelative(path)->GetContainingDirectory(); }
        c_dir->DeleteFile(FileUtil::GetFilename(path));
    }

    auto out = dir->CreateFileRelative(path);
    if (out == nullptr)
        return InstallResult::ErrorCopyFailed;
    return copy(in, out, VFS_RC_LARGE_COPY_BLOCK) ? InstallResult::Success
                                                  : InstallResult::ErrorCopyFailed;
}

bool RegisteredCache::RawInstallYuzuMeta(const CNMT& cnmt) {
    // Reasoning behind this method can be found in the comment for InstallEntry, NCA overload.
    const auto dir = this->dir->CreateDirectoryRelative("yuzu_meta");
    const auto filename = GetCNMTName(cnmt.GetType(), cnmt.GetTitleID());
    if (dir->GetFile(filename) == nullptr) {
        auto out = dir->CreateFile(filename);
        const auto buffer = cnmt.Serialize();
        out->Resize(buffer.size());
        out->WriteBytes(buffer);
    } else {
        auto out = dir->GetFile(filename);
        CNMT old_cnmt(out);
        // Returns true on change
        if (old_cnmt.UnionRecords(cnmt)) {
            out->Resize(0);
            const auto buffer = old_cnmt.Serialize();
            out->Resize(buffer.size());
            out->WriteBytes(buffer);
        }
    }
    Refresh();
    return std::find_if(yuzu_meta.begin(), yuzu_meta.end(),
                        [&cnmt](const std::pair<u64, CNMT>& kv) {
                            return kv.second.GetType() == cnmt.GetType() &&
                                   kv.second.GetTitleID() == cnmt.GetTitleID();
                        }) != yuzu_meta.end();
}

ContentProviderUnion::~ContentProviderUnion() = default;

void ContentProviderUnion::SetSlot(ContentProviderUnionSlot slot, ContentProvider* provider) {
    providers[slot] = provider;
}

void ContentProviderUnion::ClearSlot(ContentProviderUnionSlot slot) {
    providers[slot] = nullptr;
}

void ContentProviderUnion::Refresh() {
    for (auto& provider : providers) {
        if (provider.second == nullptr)
            continue;

        provider.second->Refresh();
    }
}

bool ContentProviderUnion::HasEntry(u64 title_id, ContentRecordType type) const {
    for (const auto& provider : providers) {
        if (provider.second == nullptr)
            continue;

        if (provider.second->HasEntry(title_id, type))
            return true;
    }

    return false;
}

std::optional<u32> ContentProviderUnion::GetEntryVersion(u64 title_id) const {
    for (const auto& provider : providers) {
        if (provider.second == nullptr)
            continue;

        const auto res = provider.second->GetEntryVersion(title_id);
        if (res != std::nullopt)
            return res;
    }

    return std::nullopt;
}

VirtualFile ContentProviderUnion::GetEntryUnparsed(u64 title_id, ContentRecordType type) const {
    for (const auto& provider : providers) {
        if (provider.second == nullptr)
            continue;

        const auto res = provider.second->GetEntryUnparsed(title_id, type);
        if (res != nullptr)
            return res;
    }

    return nullptr;
}

VirtualFile ContentProviderUnion::GetEntryRaw(u64 title_id, ContentRecordType type) const {
    for (const auto& provider : providers) {
        if (provider.second == nullptr)
            continue;

        const auto res = provider.second->GetEntryRaw(title_id, type);
        if (res != nullptr)
            return res;
    }

    return nullptr;
}

std::unique_ptr<NCA> ContentProviderUnion::GetEntry(u64 title_id, ContentRecordType type) const {
    for (const auto& provider : providers) {
        if (provider.second == nullptr)
            continue;

        auto res = provider.second->GetEntry(title_id, type);
        if (res != nullptr)
            return res;
    }

    return nullptr;
}

std::vector<ContentProviderEntry> ContentProviderUnion::ListEntriesFilter(
    std::optional<TitleType> title_type, std::optional<ContentRecordType> record_type,
    std::optional<u64> title_id) const {
    std::vector<ContentProviderEntry> out;

    for (const auto& provider : providers) {
        if (provider.second == nullptr)
            continue;

        const auto vec = provider.second->ListEntriesFilter(title_type, record_type, title_id);
        std::copy(vec.begin(), vec.end(), std::back_inserter(out));
    }

    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<std::pair<ContentProviderUnionSlot, ContentProviderEntry>>
ContentProviderUnion::ListEntriesFilterOrigin(std::optional<ContentProviderUnionSlot> origin,
                                              std::optional<TitleType> title_type,
                                              std::optional<ContentRecordType> record_type,
                                              std::optional<u64> title_id) const {
    std::vector<std::pair<ContentProviderUnionSlot, ContentProviderEntry>> out;

    for (const auto& provider : providers) {
        if (provider.second == nullptr)
            continue;

        if (origin.has_value() && *origin != provider.first)
            continue;

        const auto vec = provider.second->ListEntriesFilter(title_type, record_type, title_id);
        std::transform(vec.begin(), vec.end(), std::back_inserter(out),
                       [&provider](const ContentProviderEntry& entry) {
                           return std::make_pair(provider.first, entry);
                       });
    }

    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

ManualContentProvider::~ManualContentProvider() = default;

void ManualContentProvider::AddEntry(TitleType title_type, ContentRecordType content_type,
                                     u64 title_id, VirtualFile file) {
    entries.insert_or_assign({title_type, content_type, title_id}, file);
}

void ManualContentProvider::ClearAllEntries() {
    entries.clear();
}

void ManualContentProvider::Refresh() {}

bool ManualContentProvider::HasEntry(u64 title_id, ContentRecordType type) const {
    return GetEntryRaw(title_id, type) != nullptr;
}

std::optional<u32> ManualContentProvider::GetEntryVersion(u64 title_id) const {
    return std::nullopt;
}

VirtualFile ManualContentProvider::GetEntryUnparsed(u64 title_id, ContentRecordType type) const {
    return GetEntryRaw(title_id, type);
}

VirtualFile ManualContentProvider::GetEntryRaw(u64 title_id, ContentRecordType type) const {
    const auto iter =
        std::find_if(entries.begin(), entries.end(), [title_id, type](const auto& entry) {
            const auto [title_type, content_type, e_title_id] = entry.first;
            return content_type == type && e_title_id == title_id;
        });
    if (iter == entries.end())
        return nullptr;
    return iter->second;
}

std::unique_ptr<NCA> ManualContentProvider::GetEntry(u64 title_id, ContentRecordType type) const {
    const auto res = GetEntryRaw(title_id, type);
    if (res == nullptr)
        return nullptr;
    return std::make_unique<NCA>(res, nullptr, 0, keys);
}

std::vector<ContentProviderEntry> ManualContentProvider::ListEntriesFilter(
    std::optional<TitleType> title_type, std::optional<ContentRecordType> record_type,
    std::optional<u64> title_id) const {
    std::vector<ContentProviderEntry> out;

    for (const auto& entry : entries) {
        const auto [e_title_type, e_content_type, e_title_id] = entry.first;
        if ((title_type == std::nullopt || e_title_type == *title_type) &&
            (record_type == std::nullopt || e_content_type == *record_type) &&
            (title_id == std::nullopt || e_title_id == *title_id)) {
            out.emplace_back(ContentProviderEntry{e_title_id, e_content_type});
        }
    }

    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
} // namespace FileSys
