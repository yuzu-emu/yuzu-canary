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
#include "core/file_sys/program_metadata.h"
#include "core/gdbstub/gdbstub.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/resource_limit.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/nca.h"
#include "core/loader/nso.h"
#include "core/memory.h"

namespace Loader {

AppLoader_NCA::AppLoader_NCA(FileSys::VirtualFile file) : AppLoader(file) {}

FileType AppLoader_NCA::IdentifyType(const FileSys::VirtualFile& file) {
    // TODO(DarkLordZach): Assuming everything is decrypted. Add crypto support.
    FileSys::NCAHeader header{};
    if (sizeof(FileSys::NCAHeader) != file->ReadObject(&header))
        return FileType::Error;

    if (IsValidNCA(header) && header.content_type == FileSys::NCAContentType::Program)
        return FileType::NCA;

    return FileType::Error;
}

ResultStatus AppLoader_NCA::Load(Kernel::SharedPtr<Kernel::Process>& process) {
    if (is_loaded) {
        return ResultStatus::ErrorAlreadyLoaded;
    }

    nca = std::make_unique<FileSys::NCA>(file);
    ResultStatus result = nca->GetStatus();
    if (result != ResultStatus::Success) {
        return result;
    }

    if (nca->GetType() != FileSys::NCAContentType::Program)
        return ResultStatus::ErrorInvalidFormat;

    auto exefs = nca->GetExeFS();

    if (exefs == nullptr)
        return ResultStatus::ErrorInvalidFormat;

    result = metadata.Load(exefs->GetFile("main.npdm"));
    if (result != ResultStatus::Success) {
        return result;
    }
    metadata.Print();

    const FileSys::ProgramAddressSpaceType arch_bits{metadata.GetAddressSpaceType()};
    if (arch_bits == FileSys::ProgramAddressSpaceType::Is32Bit) {
        return ResultStatus::ErrorUnsupportedArch;
    }

    VAddr next_load_addr{Memory::PROCESS_IMAGE_VADDR};
    for (const auto& module : {"rtld", "main", "subsdk0", "subsdk1", "subsdk2", "subsdk3",
                               "subsdk4", "subsdk5", "subsdk6", "subsdk7", "sdk"}) {
        const VAddr load_addr = next_load_addr;

        next_load_addr = AppLoader_NSO::LoadModule(exefs->GetFile(module), load_addr);
        if (next_load_addr) {
            LOG_DEBUG(Loader, "loaded module {} @ 0x{:X}", module, load_addr);
            // Register module with GDBStub
            GDBStub::RegisterModule(module, load_addr, next_load_addr - 1, false);
        } else {
            next_load_addr = load_addr;
        }
    }

    process->program_id = metadata.GetTitleID();
    process->svc_access_mask.set();
    process->address_mappings = default_address_mappings;
    process->resource_limit =
        Kernel::ResourceLimit::GetForCategory(Kernel::ResourceLimitCategory::APPLICATION);
    process->Run(Memory::PROCESS_IMAGE_VADDR, metadata.GetMainThreadPriority(),
                 metadata.GetMainThreadStackSize());

    if (nca->GetRomFS() != nullptr && nca->GetRomFS()->GetSize() > 0)
        Service::FileSystem::RegisterRomFS(std::make_unique<FileSys::RomFSFactory>(*this));

    is_loaded = true;

    return ResultStatus::Success;
}

ResultStatus AppLoader_NCA::ReadRomFS(FileSys::VirtualFile& dir) {
    if (nca == nullptr || nca->GetRomFS() == nullptr || nca->GetRomFS()->GetSize() == 0)
        return ResultStatus::ErrorNotUsed;
    dir = nca->GetRomFS();
    return ResultStatus::Success;
}

AppLoader_NCA::~AppLoader_NCA() = default;

} // namespace Loader
