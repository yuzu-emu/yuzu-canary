// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <vector>

#include "common/common_types.h"
#include "core/file_sys/card_image.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/romfs.h"
#include "core/hle/kernel/process.h"
#include "core/loader/nca.h"
#include "core/loader/xci.h"

namespace Loader {

AppLoader_XCI::AppLoader_XCI(FileSys::VirtualFile file)
    : AppLoader(file), xci(std::make_unique<FileSys::XCI>(file)),
      nca_loader(std::make_unique<AppLoader_NCA>(
          xci->GetNCAFileByType(FileSys::NCAContentType::Program))) {
    if (xci->GetStatus() != ResultStatus::Success)
        return;
    const auto control_nca = xci->GetNCAByType(FileSys::NCAContentType::Control);
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

AppLoader_XCI::~AppLoader_XCI() = default;

FileType AppLoader_XCI::IdentifyType(const FileSys::VirtualFile& file) {
    FileSys::XCI xci(file);

    if (xci.GetStatus() == ResultStatus::Success &&
        xci.GetNCAByType(FileSys::NCAContentType::Program) != nullptr &&
        AppLoader_NCA::IdentifyType(xci.GetNCAFileByType(FileSys::NCAContentType::Program)) ==
            FileType::NCA) {
        return FileType::XCI;
    }

    return FileType::Error;
}

ResultStatus AppLoader_XCI::Load(Kernel::SharedPtr<Kernel::Process>& process) {
    if (is_loaded) {
        return ResultStatus::ErrorAlreadyLoaded;
    }

    if (xci->GetStatus() != ResultStatus::Success)
        return xci->GetStatus();

    if (xci->GetProgramNCAStatus() != ResultStatus::Success)
        return xci->GetProgramNCAStatus();

    const auto nca = xci->GetNCAFileByType(FileSys::NCAContentType::Program);
    if (nca == nullptr && !Core::Crypto::KeyManager::KeyFileExists(false))
        return ResultStatus::ErrorMissingProductionKeyFile;

    auto result = nca_loader->Load(process);
    if (result != ResultStatus::Success)
        return result;

    is_loaded = true;

    return ResultStatus::Success;
}

ResultStatus AppLoader_XCI::ReadRomFS(FileSys::VirtualFile& dir) {
    return nca_loader->ReadRomFS(dir);
}

ResultStatus AppLoader_XCI::ReadProgramId(u64& out_program_id) {
    return nca_loader->ReadProgramId(out_program_id);
}

ResultStatus AppLoader_XCI::ReadIcon(std::vector<u8>& buffer) {
    if (icon_file == nullptr)
        return ResultStatus::ErrorNoControl;
    buffer = icon_file->ReadAllBytes();
    return ResultStatus::Success;
}

ResultStatus AppLoader_XCI::ReadTitle(std::string& title) {
    if (nacp_file == nullptr)
        return ResultStatus::ErrorNoControl;
    title = nacp_file->GetApplicationName();
    return ResultStatus::Success;
}
} // namespace Loader
