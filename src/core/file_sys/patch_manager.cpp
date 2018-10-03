// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>

#include "common/hex_util.h"
#include "common/logging/log.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/ips_layer.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/vfs_layered.h"
#include "core/file_sys/vfs_vector.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/loader.h"

namespace FileSys {

constexpr u64 SINGLE_BYTE_MODULUS = 0x100;
constexpr u64 DLC_BASE_TITLE_ID_MASK = 0xFFFFFFFFFFFFE000;

struct NSOBuildHeader {
    u32_le magic;
    INSERT_PADDING_BYTES(0x3C);
    std::array<u8, 0x20> build_id;
    INSERT_PADDING_BYTES(0xA0);
};
static_assert(sizeof(NSOBuildHeader) == 0x100, "NSOBuildHeader has incorrect size.");

std::string FormatTitleVersion(u32 version, TitleVersionFormat format) {
    std::array<u8, sizeof(u32)> bytes{};
    bytes[0] = version % SINGLE_BYTE_MODULUS;
    for (std::size_t i = 1; i < bytes.size(); ++i) {
        version /= SINGLE_BYTE_MODULUS;
        bytes[i] = version % SINGLE_BYTE_MODULUS;
    }

    if (format == TitleVersionFormat::FourElements)
        return fmt::format("v{}.{}.{}.{}", bytes[3], bytes[2], bytes[1], bytes[0]);
    return fmt::format("v{}.{}.{}", bytes[3], bytes[2], bytes[1]);
}

PatchManager::PatchManager(u64 title_id) : title_id(title_id) {}

PatchManager::~PatchManager() = default;

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

static std::vector<VirtualFile> CollectIPSPatches(const std::vector<VirtualDir>& patch_dirs,
                                                  const std::string& build_id) {
    std::vector<VirtualFile> ips;
    ips.reserve(patch_dirs.size());
    for (const auto& subdir : patch_dirs) {
        auto exefs_dir = subdir->GetSubdirectory("exefs");
        if (exefs_dir != nullptr) {
            for (const auto& file : exefs_dir->GetFiles()) {
                if (file->GetExtension() != "ips")
                    continue;
                auto name = file->GetName();
                const auto p1 = name.substr(0, name.find('.'));
                const auto this_build_id = p1.substr(0, p1.find_last_not_of('0') + 1);

                if (build_id == this_build_id)
                    ips.push_back(file);
            }
        }
    }

    return ips;
}

std::vector<u8> PatchManager::PatchNSO(const std::vector<u8>& nso) const {
    if (nso.size() < 0x100)
        return nso;

    NSOBuildHeader header;
    std::memcpy(&header, nso.data(), sizeof(NSOBuildHeader));

    if (header.magic != Common::MakeMagic('N', 'S', 'O', '0'))
        return nso;

    const auto build_id_raw = Common::HexArrayToString(header.build_id);
    const auto build_id = build_id_raw.substr(0, build_id_raw.find_last_not_of('0') + 1);

    LOG_INFO(Loader, "Patching NSO for build_id={}", build_id);

    const auto load_dir = Service::FileSystem::GetModificationLoadRoot(title_id);
    auto patch_dirs = load_dir->GetSubdirectories();
    std::sort(patch_dirs.begin(), patch_dirs.end(),
              [](const VirtualDir& l, const VirtualDir& r) { return l->GetName() < r->GetName(); });
    const auto ips = CollectIPSPatches(patch_dirs, build_id);

    auto out = nso;
    for (const auto& ips_file : ips) {
        LOG_INFO(Loader, "    - Appling IPS patch from mod \"{}\"",
                 ips_file->GetContainingDirectory()->GetParentDirectory()->GetName());
        const auto patched = PatchIPS(std::make_shared<VectorVfsFile>(out), ips_file);
        if (patched != nullptr)
            out = patched->ReadAllBytes();
    }

    if (out.size() < 0x100)
        return nso;
    std::memcpy(out.data(), &header, sizeof(NSOBuildHeader));
    return out;
}

bool PatchManager::HasNSOPatch(const std::array<u8, 32>& build_id_) const {
    const auto build_id_raw = Common::HexArrayToString(build_id_);
    const auto build_id = build_id_raw.substr(0, build_id_raw.find_last_not_of('0') + 1);

    LOG_INFO(Loader, "Querying NSO patch existence for build_id={}", build_id);

    const auto load_dir = Service::FileSystem::GetModificationLoadRoot(title_id);
    auto patch_dirs = load_dir->GetSubdirectories();
    std::sort(patch_dirs.begin(), patch_dirs.end(),
              [](const VirtualDir& l, const VirtualDir& r) { return l->GetName() < r->GetName(); });

    return !CollectIPSPatches(patch_dirs, build_id).empty();
}

static void ApplyLayeredFS(VirtualFile& romfs, u64 title_id, ContentRecordType type) {
    const auto load_dir = Service::FileSystem::GetModificationLoadRoot(title_id);
    if (type != ContentRecordType::Program || load_dir == nullptr || load_dir->GetSize() <= 0) {
        return;
    }

    auto extracted = ExtractRomFS(romfs);
    if (extracted == nullptr) {
        return;
    }

    auto patch_dirs = load_dir->GetSubdirectories();
    std::sort(patch_dirs.begin(), patch_dirs.end(),
              [](const VirtualDir& l, const VirtualDir& r) { return l->GetName() < r->GetName(); });

    std::vector<VirtualDir> layers;
    layers.reserve(patch_dirs.size() + 1);
    for (const auto& subdir : patch_dirs) {
        auto romfs_dir = subdir->GetSubdirectory("romfs");
        if (romfs_dir != nullptr)
            layers.push_back(std::move(romfs_dir));
    }
    layers.push_back(std::move(extracted));

    auto layered = LayeredVfsDirectory::MakeLayeredDirectory(std::move(layers));
    if (layered == nullptr) {
        return;
    }

    auto packed = CreateRomFS(std::move(layered));
    if (packed == nullptr) {
        return;
    }

    LOG_INFO(Loader, "    RomFS: LayeredFS patches applied successfully");
    romfs = std::move(packed);
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

    // LayeredFS
    ApplyLayeredFS(romfs, title_id, type);

    return romfs;
}

static void AppendCommaIfNotEmpty(std::string& to, const std::string& with) {
    if (to.empty())
        to += with;
    else
        to += ", " + with;
}

static bool IsDirValidAndNonEmpty(const VirtualDir& dir) {
    return dir != nullptr && (!dir->GetFiles().empty() || !dir->GetSubdirectories().empty());
}

std::map<std::string, std::string, std::less<>> PatchManager::GetPatchVersionNames() const {
    std::map<std::string, std::string, std::less<>> out;
    const auto installed = Service::FileSystem::GetUnionContents();

    // Game Updates
    const auto update_tid = GetUpdateTitleID(title_id);
    PatchManager update{update_tid};
    auto [nacp, discard_icon_file] = update.GetControlMetadata();

    if (nacp != nullptr) {
        out.insert_or_assign("Update", nacp->GetVersionString());
    } else {
        if (installed->HasEntry(update_tid, ContentRecordType::Program)) {
            const auto meta_ver = installed->GetEntryVersion(update_tid);
            if (meta_ver == boost::none || meta_ver.get() == 0) {
                out.insert_or_assign("Update", "");
            } else {
                out.insert_or_assign(
                    "Update",
                    FormatTitleVersion(meta_ver.get(), TitleVersionFormat::ThreeElements));
            }
        }
    }

    // General Mods (LayeredFS and IPS)
    const auto mod_dir = Service::FileSystem::GetModificationLoadRoot(title_id);
    if (mod_dir != nullptr && mod_dir->GetSize() > 0) {
        for (const auto& mod : mod_dir->GetSubdirectories()) {
            std::string types;
            if (IsDirValidAndNonEmpty(mod->GetSubdirectory("exefs")))
                AppendCommaIfNotEmpty(types, "IPS");
            if (IsDirValidAndNonEmpty(mod->GetSubdirectory("romfs")))
                AppendCommaIfNotEmpty(types, "LayeredFS");

            if (types.empty())
                continue;

            out.insert_or_assign(mod->GetName(), types);
        }
    }

    // DLC
    const auto dlc_entries = installed->ListEntriesFilter(TitleType::AOC, ContentRecordType::Data);
    std::vector<RegisteredCacheEntry> dlc_match;
    dlc_match.reserve(dlc_entries.size());
    std::copy_if(dlc_entries.begin(), dlc_entries.end(), std::back_inserter(dlc_match),
                 [this, &installed](const RegisteredCacheEntry& entry) {
                     return (entry.title_id & DLC_BASE_TITLE_ID_MASK) == title_id &&
                            installed->GetEntry(entry)->GetStatus() ==
                                Loader::ResultStatus::Success;
                 });
    if (!dlc_match.empty()) {
        // Ensure sorted so DLC IDs show in order.
        std::sort(dlc_match.begin(), dlc_match.end());

        std::string list;
        for (size_t i = 0; i < dlc_match.size() - 1; ++i)
            list += fmt::format("{}, ", dlc_match[i].title_id & 0x7FF);

        list += fmt::format("{}", dlc_match.back().title_id & 0x7FF);

        out.insert_or_assign("DLC", std::move(list));
    }

    return out;
}

std::pair<std::shared_ptr<NACP>, VirtualFile> PatchManager::GetControlMetadata() const {
    const auto& installed{Service::FileSystem::GetUnionContents()};

    const auto base_control_nca = installed->GetEntry(title_id, ContentRecordType::Control);
    if (base_control_nca == nullptr)
        return {};

    return ParseControlNCA(base_control_nca);
}

std::pair<std::shared_ptr<NACP>, VirtualFile> PatchManager::ParseControlNCA(
    const std::shared_ptr<NCA>& nca) const {
    const auto base_romfs = nca->GetRomFS();
    if (base_romfs == nullptr)
        return {};

    const auto romfs = PatchRomFS(base_romfs, nca->GetBaseIVFCOffset(), ContentRecordType::Control);
    if (romfs == nullptr)
        return {};

    const auto extracted = ExtractRomFS(romfs);
    if (extracted == nullptr)
        return {};

    auto nacp_file = extracted->GetFile("control.nacp");
    if (nacp_file == nullptr)
        nacp_file = extracted->GetFile("Control.nacp");

    const auto nacp = nacp_file == nullptr ? nullptr : std::make_shared<NACP>(nacp_file);

    VirtualFile icon_file;
    for (const auto& language : FileSys::LANGUAGE_NAMES) {
        icon_file = extracted->GetFile("icon_" + std::string(language) + ".dat");
        if (icon_file != nullptr)
            break;
    }

    return {nacp, icon_file};
}
} // namespace FileSys
