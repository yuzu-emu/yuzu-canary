// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <vector>

#include "common/common_types.h"
#include "core/file_sys/card_image.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/submission_package.h"
#include "core/hle/kernel/process.h"
#include "core/loader/deconstructed_rom_directory.h"
#include "core/loader/nca.h"
#include "core/loader/nsp.h"

namespace Loader {

AppLoader_NSP::AppLoader_NSP(FileSys::VirtualFile file)
    : AppLoader(file), nsp(std::make_unique<FileSys::NSP>(file)),
      title_id(nsp->GetProgramTitleID()) {

    if (nsp->GetStatus() != ResultStatus::Success)
        return;
    if (nsp->IsExtractedType())
        return;

    const auto control_nca =
        nsp->GetNCA(nsp->GetFirstTitleID(), FileSys::ContentRecordType::Control);
    if (control_nca == nullptr || control_nca->GetStatus() != ResultStatus::Success)
        return;

    const auto romfs = FileSys::ExtractRomFS(control_nca->GetRomFS());
    if (romfs == nullptr)
        return;

    for (const auto& language : FileSys::LANGUAGE_NAMES) {
        icon_file = romfs->GetFile("icon_" + std::string(language) + ".dat");
        if (icon_file != nullptr)
            break;
    }

    const auto nacp_raw = romfs->GetFile("control.nacp");
    if (nacp_raw == nullptr)
        return;
    nacp_file = std::make_shared<FileSys::NACP>(nacp_raw);
}

AppLoader_NSP::~AppLoader_NSP() = default;

FileType AppLoader_NSP::IdentifyType(const FileSys::VirtualFile& file) {
    FileSys::NSP nsp(file);

    if (nsp.GetStatus() == ResultStatus::Success) {
        // Extracted Type case
        if (nsp.IsExtractedType() && nsp.GetExeFS() != nullptr &&
            FileSys::IsDirectoryExeFS(nsp.GetExeFS()) && nsp.GetRomFS() != nullptr) {
            return FileType::NSP;
        }

        // Non-Ectracted Type case
        if (!nsp.IsExtractedType() &&
            nsp.GetNCA(nsp.GetFirstTitleID(), FileSys::ContentRecordType::Program) != nullptr &&
            AppLoader_NCA::IdentifyType(nsp.GetNCAFile(
                nsp.GetFirstTitleID(), FileSys::ContentRecordType::Program)) == FileType::NCA) {
            return FileType::NSP;
        }
    }

    return FileType::Error;
}

ResultStatus AppLoader_NSP::Load(Kernel::SharedPtr<Kernel::Process>& process) {
    if (is_loaded) {
        return ResultStatus::ErrorAlreadyLoaded;
    }

    if (nsp->IsExtractedType()) {
        secondary_loader = std::make_unique<AppLoader_DeconstructedRomDirectory>(nsp->GetExeFS());
    } else {
        if (title_id == 0)
            return ResultStatus::ErrorNSPMissingProgramNCA;

        secondary_loader = std::make_unique<AppLoader_NCA>(
            nsp->GetNCAFile(title_id, FileSys::ContentRecordType::Program));

        if (nsp->GetStatus() != ResultStatus::Success)
            return nsp->GetStatus();

        if (nsp->GetProgramStatus(title_id) != ResultStatus::Success)
            return nsp->GetProgramStatus(title_id);

        if (nsp->GetNCA(title_id, FileSys::ContentRecordType::Program) == nullptr) {
            if (!Core::Crypto::KeyManager::KeyFileExists(false))
                return ResultStatus::ErrorMissingProductionKeyFile;
            return ResultStatus::ErrorNSPMissingProgramNCA;
        }
    }

    const auto result = secondary_loader->Load(process);
    if (result != ResultStatus::Success)
        return result;

    is_loaded = true;

    return ResultStatus::Success;
}

ResultStatus AppLoader_NSP::ReadRomFS(FileSys::VirtualFile& dir) {
    return secondary_loader->ReadRomFS(dir);
}

ResultStatus AppLoader_NSP::ReadProgramId(u64& out_program_id) {
    if (title_id == 0)
        return ResultStatus::ErrorNotInitialized;
    out_program_id = title_id;
    return ResultStatus::Success;
}

ResultStatus AppLoader_NSP::ReadIcon(std::vector<u8>& buffer) {
    if (icon_file == nullptr)
        return ResultStatus::ErrorNoControl;
    buffer = icon_file->ReadAllBytes();
    return ResultStatus::Success;
}

ResultStatus AppLoader_NSP::ReadTitle(std::string& title) {
    if (nacp_file == nullptr)
        return ResultStatus::ErrorNoControl;
    title = nacp_file->GetApplicationName();
    return ResultStatus::Success;
}
} // namespace Loader
