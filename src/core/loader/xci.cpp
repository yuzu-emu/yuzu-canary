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
          xci->GetNCAFileByType(FileSys::NCAContentType::Program))) {}

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
        return ResultStatus::ErrorEncrypted;
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

} // namespace Loader
