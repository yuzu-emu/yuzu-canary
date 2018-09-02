// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <fmt/ostream.h>
#include "common/assert.h"
#include "common/hex_util.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/submission_package.h"
#include "core/loader/loader.h"

namespace FileSys {
NSP::NSP(VirtualFile file_)
    : file(std::move(file_)),
      pfs(std::make_shared<PartitionFilesystem>(file)), status{Loader::ResultStatus::Success} {
    if (pfs->GetStatus() != Loader::ResultStatus::Success) {
        status = pfs->GetStatus();
        return;
    }

    if (IsDirectoryExeFS(pfs)) {
        extracted = true;
        exefs = pfs;

        const auto& files = pfs->GetFiles();
        const auto romfs_iter =
            std::find_if(files.begin(), files.end(), [](const FileSys::VirtualFile& file) {
                return file->GetName().find(".romfs") != std::string::npos;
            });
        if (romfs_iter != files.end())
            romfs = *romfs_iter;
        return;
    }

    extracted = false;
    const auto files = pfs->GetFiles();

    Core::Crypto::KeyManager keys;
    for (const auto& ticket_file : files) {
        if (ticket_file->GetExtension() == "tik") {
            if (ticket_file == nullptr ||
                ticket_file->GetSize() <
                    Core::Crypto::TICKET_FILE_TITLEKEY_OFFSET + sizeof(Core::Crypto::Key128)) {
                continue;
            }

            Core::Crypto::Key128 key{};
            ticket_file->Read(key.data(), key.size(), Core::Crypto::TICKET_FILE_TITLEKEY_OFFSET);
            std::string_view name_only(ticket_file->GetName());
            name_only.remove_suffix(4);
            const auto rights_id_raw = Common::HexStringToArray<16>(name_only);
            u128 rights_id;
            std::memcpy(rights_id.data(), rights_id_raw.data(), sizeof(u128));
            keys.SetKey(Core::Crypto::S128KeyType::Titlekey, key, rights_id[1], rights_id[0]);
        }
    }

    for (const auto& outer_file : files) {
        if (outer_file->GetName().substr(outer_file->GetName().size() - 9) == ".cnmt.nca") {
            const auto nca = std::make_shared<NCA>(outer_file);
            if (nca->GetStatus() != Loader::ResultStatus::Success)
                continue;
            const auto section0 = nca->GetSubdirectories()[0];

            for (const auto& inner_file : section0->GetFiles()) {
                if (inner_file->GetExtension() != "cnmt")
                    continue;

                const CNMT cnmt(inner_file);
                auto& ncas_title = ncas[cnmt.GetTitleID()];

                ncas_title[ContentRecordType::Meta] = nca;
                for (const auto& rec : cnmt.GetContentRecords()) {
                    const auto id_string = Common::HexArrayToString(rec.nca_id, false);
                    const auto next_file = pfs->GetFile(fmt::format("{}.nca", id_string));
                    if (next_file == nullptr) {
                        LOG_WARNING(Service_FS,
                                    "NCA with ID {}.nca is listed in content metadata, but cannot "
                                    "be found in PFS. NSP appears to be corrupted.",
                                    id_string);
                        continue;
                    }

                    auto next_nca = std::make_shared<NCA>(next_file);
                    if (next_nca->GetType() == NCAContentType::Program)
                        program_status[cnmt.GetTitleID()] = next_nca->GetStatus();
                    if (next_nca->GetStatus() == Loader::ResultStatus::Success)
                        ncas_title[rec.type] = std::move(next_nca);
                }

                break;
            }
        }
    }
}

NSP::~NSP() = default;

Loader::ResultStatus NSP::GetStatus() const {
    return status;
}

Loader::ResultStatus NSP::GetProgramStatus(u64 title_id) const {
    const auto iter = program_status.find(title_id);
    if (iter == program_status.end())
        return Loader::ResultStatus::ErrorNSPMissingProgramNCA;
    return iter->second;
}

u64 NSP::GetFirstTitleID() const {
    if (program_status.empty())
        return 0;
    return program_status.begin()->first;
}

u64 NSP::GetProgramTitleID() const {
    const auto out = GetFirstTitleID();
    if ((out & 0x800) == 0)
        return out;

    const auto ids = GetTitleIDs();
    const auto iter =
        std::find_if(ids.begin(), ids.end(), [](u64 tid) { return (tid & 0x800) == 0; });
    return iter == ids.end() ? out : *iter;
}

std::vector<u64> NSP::GetTitleIDs() const {
    std::vector<u64> out;
    out.reserve(ncas.size());
    for (const auto& kv : ncas)
        out.push_back(kv.first);
    return out;
}

bool NSP::IsExtractedType() const {
    return extracted;
}

VirtualFile NSP::GetRomFS() const {
    return romfs;
}

VirtualDir NSP::GetExeFS() const {
    return exefs;
}

std::vector<std::shared_ptr<NCA>> NSP::GetNCAsCollapsed() const {
    if (extracted)
        LOG_WARNING(Service_FS, "called on an NSP that is of type extracted.");
    std::vector<std::shared_ptr<NCA>> out;
    for (const auto& map : ncas) {
        for (const auto& inner_map : map.second)
            out.push_back(inner_map.second);
    }
    return out;
}

std::multimap<u64, std::shared_ptr<NCA>> NSP::GetNCAsByTitleID() const {
    if (extracted)
        LOG_WARNING(Service_FS, "called on an NSP that is of type extracted.");
    std::multimap<u64, std::shared_ptr<NCA>> out;
    for (const auto& map : ncas) {
        for (const auto& inner_map : map.second)
            out.emplace(map.first, inner_map.second);
    }
    return out;
}

std::map<u64, std::map<ContentRecordType, std::shared_ptr<NCA>>> NSP::GetNCAs() const {
    return ncas;
}

std::shared_ptr<NCA> NSP::GetNCA(u64 title_id, ContentRecordType type) const {
    if (extracted)
        LOG_WARNING(Service_FS, "called on an NSP that is of type extracted.");

    const auto title_id_iter = ncas.find(title_id);
    if (title_id_iter == ncas.end())
        return nullptr;

    const auto type_iter = title_id_iter->second.find(type);
    if (type_iter == title_id_iter->second.end())
        return nullptr;

    return type_iter->second;
}

VirtualFile NSP::GetNCAFile(u64 title_id, ContentRecordType type) const {
    if (extracted)
        LOG_WARNING(Service_FS, "called on an NSP that is of type extracted.");
    const auto nca = GetNCA(title_id, type);
    if (nca != nullptr)
        return nca->GetBaseFile();
    return nullptr;
}

std::vector<Core::Crypto::Key128> NSP::GetTitlekey() const {
    if (extracted)
        LOG_WARNING(Service_FS, "called on an NSP that is of type extracted.");
    std::vector<Core::Crypto::Key128> out;
    for (const auto& ticket_file : ticket_files) {
        if (ticket_file == nullptr ||
            ticket_file->GetSize() <
                Core::Crypto::TICKET_FILE_TITLEKEY_OFFSET + sizeof(Core::Crypto::Key128)) {
            continue;
        }

        out.emplace_back();
        ticket_file->Read(out.back().data(), out.back().size(),
                          Core::Crypto::TICKET_FILE_TITLEKEY_OFFSET);
    }
    return out;
}

std::vector<VirtualFile> NSP::GetFiles() const {
    return pfs->GetFiles();
}

std::vector<VirtualDir> NSP::GetSubdirectories() const {
    return pfs->GetSubdirectories();
}

std::string NSP::GetName() const {
    return file->GetName();
}

VirtualDir NSP::GetParentDirectory() const {
    return file->GetContainingDirectory();
}

bool NSP::ReplaceFileWithSubdirectory(VirtualFile file, VirtualDir dir) {
    return false;
}
} // namespace FileSys
