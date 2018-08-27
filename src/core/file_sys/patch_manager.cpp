// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/hle/service/filesystem/filesystem.h"

namespace FileSys {

constexpr u64 SINGLE_BYTE_MODULUS = 0x100;

std::string FormatTitleVersion(u32 version, TitleVersionFormat format) {
    std::array<u8, sizeof(u32)> bytes{};
    bytes[0] = version % SINGLE_BYTE_MODULUS;
    for (size_t i = 1; i < bytes.size(); ++i) {
        version /= SINGLE_BYTE_MODULUS;
        bytes[i] = version % SINGLE_BYTE_MODULUS;
    }

    if (format == TitleVersionFormat::FourElements)
        return fmt::format("v{}.{}.{}.{}", bytes[3], bytes[2], bytes[1], bytes[0]);
    return fmt::format("v{}.{}.{}", bytes[3], bytes[2], bytes[1]);
}

constexpr std::array<const char*, 1> PATCH_TYPE_NAMES{
    "Update",
};

std::string FormatPatchTypeName(PatchType type) {
    return PATCH_TYPE_NAMES.at(static_cast<size_t>(type));
}

PatchManager::PatchManager(u64 title_id) : title_id(title_id) {}

VirtualDir PatchManager::PatchExeFS(VirtualDir exefs) const {
    if (exefs == nullptr)
        return exefs;

    const auto installed = Service::FileSystem::GetUnionContents();

    // Game Updates
    const auto update_tid = GetUpdateTitleID(title_id);
    const auto update = installed->GetEntry(update_tid, ContentRecordType::Program);
    if (update != nullptr) {
        if (update->GetStatus() == Loader::ResultStatus::ErrorMissingBKTRBaseRomFS &&
            update->GetExeFS() != nullptr) {
            exefs = update->GetExeFS();
        }
    }

    return exefs;
}

VirtualFile PatchManager::PatchRomFS(VirtualFile romfs) const {
    if (romfs == nullptr)
        return romfs;

    const auto installed = Service::FileSystem::GetUnionContents();

    // Game Updates
    const auto update_tid = GetUpdateTitleID(title_id);
    const auto update = installed->GetEntryRaw(update_tid, ContentRecordType::Program);
    if (update != nullptr) {
        const auto nca = std::make_shared<NCA>(update, romfs);
        if (nca->GetStatus() == Loader::ResultStatus::Success && nca->GetRomFS() != nullptr)
            romfs = nca->GetRomFS();
    }

    return romfs;
}

std::map<PatchType, u32> PatchManager::GetPatchVersionNames() const {
    std::map<PatchType, u32> out;
    const auto installed = Service::FileSystem::GetUnionContents();

    const auto update_tid = GetUpdateTitleID(title_id);
    const auto update_version = installed->GetEntryVersion(update_tid);
    if (update_version != boost::none &&
        installed->HasEntry(update_tid, ContentRecordType::Program)) {
        out[PatchType::Update] = update_version.get();
    }

    return out;
}

} // namespace FileSys
