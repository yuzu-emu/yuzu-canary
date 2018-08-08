// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <vector>

#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "common/swap.h"
#include "core/core.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/program_metadata.h"
#include "core/file_sys/romfs.h"
#include "core/gdbstub/gdbstub.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/resource_limit.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/nso.h"
#include "core/loader/xci.h"
#include "core/memory.h"

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

    if (xci->GetNCAFileByType(FileSys::NCAContentType::Program) == nullptr) {
        if (!Core::Crypto::KeyManager::KeyFileExists(false))
            return ResultStatus::ErrorMissingKeys;
        return ResultStatus::ErrorDecrypting;
    }

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
        return ResultStatus::ErrorInvalidFormat;
    buffer = icon_file->ReadAllBytes();
    return ResultStatus::Success;
}

ResultStatus AppLoader_XCI::ReadTitle(std::string& title) {
    if (nacp_file == nullptr)
        return ResultStatus::ErrorInvalidFormat;
    title = nacp_file->GetApplicationName();
    return ResultStatus::Success;
}
} // namespace Loader
