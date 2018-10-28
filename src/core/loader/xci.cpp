// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <vector>

#include "common/common_types.h"
#include "core/file_sys/card_image.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/submission_package.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/nca.h"
#include "core/loader/xci.h"

namespace Loader {

AppLoader_XCI::AppLoader_XCI(FileSys::VirtualFile file)
    : AppLoader(file), xci(std::make_unique<FileSys::XCI>(file)),
      nca_loader(std::make_unique<AppLoader_NCA>(xci->GetProgramNCAFile())) {
    if (xci->GetStatus() != ResultStatus::Success)
        return;

    const auto control_nca = xci->GetNCAByType(FileSys::NCAContentType::Control);
    if (control_nca == nullptr || control_nca->GetStatus() != ResultStatus::Success)
        return;

    std::tie(nacp_file, icon_file) =
        FileSys::PatchManager(xci->GetProgramTitleID()).ParseControlNCA(*control_nca);
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

ResultStatus AppLoader_XCI::Load(Kernel::Process& process) {
    if (is_loaded) {
        return ResultStatus::ErrorAlreadyLoaded;
    }

    if (xci->GetStatus() != ResultStatus::Success)
        return xci->GetStatus();

    if (xci->GetProgramNCAStatus() != ResultStatus::Success)
        return xci->GetProgramNCAStatus();

    if (!xci->HasProgramNCA() && !Core::Crypto::KeyManager::KeyFileExists(false))
        return ResultStatus::ErrorMissingProductionKeyFile;

    const auto result = nca_loader->Load(process);
    if (result != ResultStatus::Success)
        return result;

    FileSys::VirtualFile update_raw;
    if (ReadUpdateRaw(update_raw) == ResultStatus::Success && update_raw != nullptr)
        Service::FileSystem::SetPackedUpdate(std::move(update_raw));

    is_loaded = true;

    return ResultStatus::Success;
}

ResultStatus AppLoader_XCI::ReadRomFS(FileSys::VirtualFile& file) {
    return nca_loader->ReadRomFS(file);
}

u64 AppLoader_XCI::ReadRomFSIVFCOffset() const {
    return nca_loader->ReadRomFSIVFCOffset();
}

ResultStatus AppLoader_XCI::ReadUpdateRaw(FileSys::VirtualFile& file) {
    u64 program_id{};
    nca_loader->ReadProgramId(program_id);
    if (program_id == 0)
        return ResultStatus::ErrorXCIMissingProgramNCA;

    const auto read = xci->GetSecurePartitionNSP()->GetNCAFile(
        FileSys::GetUpdateTitleID(program_id), FileSys::ContentRecordType::Program);

    if (read == nullptr)
        return ResultStatus::ErrorNoPackedUpdate;
    const auto nca_test = std::make_shared<FileSys::NCA>(read);

    if (nca_test->GetStatus() != ResultStatus::ErrorMissingBKTRBaseRomFS)
        return nca_test->GetStatus();

    file = read;
    return ResultStatus::Success;
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

ResultStatus AppLoader_XCI::ReadDeveloper(std::string& developer) {
    if (nacp_file == nullptr)
        return ResultStatus::ErrorNoControl;
    developer = nacp_file->GetDeveloperName();
    return ResultStatus::Success;
}
} // namespace Loader
