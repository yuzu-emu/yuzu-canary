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
    LOG_INFO(Loader, "Patching ExeFS for title_id={:016X}", title_id);

    if (exefs == nullptr)
        return exefs;

    const auto installed = Service::FileSystem::GetUnionContents();

    // Game Updates
    const auto update_tid = GetUpdateTitleID(title_id);
    const auto update = installed->GetEntry(update_tid, ContentRecordType::Program);
    if (update != nullptr) {
        if (update->GetStatus() == Loader::ResultStatus::ErrorMissingBKTRBaseRomFS &&
            update->GetExeFS() != nullptr) {
            LOG_INFO(Loader, "    ExeFS: Update ({}) applied successfully",
                     FormatTitleVersion(installed->GetEntryVersion(update_tid).get_value_or(0)));
            exefs = update->GetExeFS();
        }
    }

    return exefs;
}

VirtualFile PatchManager::PatchRomFS(VirtualFile romfs, u64 ivfc_offset,
                                     ContentRecordType type) const {
    LOG_INFO(Loader, "Patching RomFS for title_id={:016X}, type={:02X}", title_id,
             static_cast<u8>(type));

    if (romfs == nullptr)
        return romfs;

    const auto installed = Service::FileSystem::GetUnionContents();

    // Game Updates
    const auto update_tid = GetUpdateTitleID(title_id);
    const auto update = installed->GetEntryRaw(update_tid, type);
    if (update != nullptr) {
        const auto new_nca = std::make_shared<NCA>(update, romfs, ivfc_offset);
        if (new_nca->GetStatus() == Loader::ResultStatus::Success &&
            new_nca->GetRomFS() != nullptr) {
            LOG_INFO(Loader, "    RomFS: Update ({}) applied successfully",
                     FormatTitleVersion(installed->GetEntryVersion(update_tid).get_value_or(0)));
            romfs = new_nca->GetRomFS();
        }
    }

    return romfs;
}

std::map<PatchType, std::string> PatchManager::GetPatchVersionNames() const {
    std::map<PatchType, std::string> out;
    const auto installed = Service::FileSystem::GetUnionContents();

    const auto update_tid = GetUpdateTitleID(title_id);
    const auto update_control = installed->GetEntry(title_id, ContentRecordType::Control);
    if (update_control != nullptr) {
        do {
            const auto romfs =
                PatchRomFS(update_control->GetRomFS(), update_control->GetBaseIVFCOffset(),
                           FileSys::ContentRecordType::Control);
            if (romfs == nullptr)
                break;

            const auto control_dir = FileSys::ExtractRomFS(romfs);
            if (control_dir == nullptr)
                break;

            const auto nacp_file = control_dir->GetFile("control.nacp");
            if (nacp_file == nullptr)
                break;

            FileSys::NACP nacp(nacp_file);
            out[PatchType::Update] = nacp.GetVersionString();
        } while (false);
    }

    return out;
}

} // namespace FileSys
