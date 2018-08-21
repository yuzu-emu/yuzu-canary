// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "core/core.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/xts_archive.h"
#include "core/hle/kernel/process.h"
#include "core/loader/nax.h"
#include "core/loader/nca.h"

namespace Loader {

AppLoader_NAX::AppLoader_NAX(FileSys::VirtualFile file)
    : AppLoader(file), nax(std::make_unique<FileSys::NAX>(file)),
      nca_loader(std::make_unique<AppLoader_NCA>(nax->GetDecrypted())) {}

AppLoader_NAX::~AppLoader_NAX() = default;

FileType AppLoader_NAX::IdentifyType(const FileSys::VirtualFile& file) {
    FileSys::NAX nax(file);

    if (nax.GetStatus() == ResultStatus::Success && nax.AsNCA() != nullptr &&
        nax.AsNCA()->GetStatus() == ResultStatus::Success) {
        return FileType::NAX;
    }

    return FileType::Error;
}

ResultStatus AppLoader_NAX::Load(Kernel::SharedPtr<Kernel::Process>& process) {
    if (is_loaded) {
        return ResultStatus::ErrorAlreadyLoaded;
    }

    if (nax->GetStatus() != ResultStatus::Success)
        return nax->GetStatus();

    const auto nca = nax->AsNCA();
    if (nca == nullptr) {
        if (!Core::Crypto::KeyManager::KeyFileExists(false))
            return ResultStatus::ErrorMissingProductionKeyFile;
        return ResultStatus::ErrorNAXInconvertibleToNCA;
    }

    if (nca->GetStatus() != ResultStatus::Success)
        return nca->GetStatus();

    const auto result = nca_loader->Load(process);
    if (result != ResultStatus::Success)
        return result;

    is_loaded = true;

    return ResultStatus::Success;
}

ResultStatus AppLoader_NAX::ReadRomFS(FileSys::VirtualFile& dir) {
    return nca_loader->ReadRomFS(dir);
}

ResultStatus AppLoader_NAX::ReadProgramId(u64& out_program_id) {
    return nca_loader->ReadProgramId(out_program_id);
}
} // namespace Loader
