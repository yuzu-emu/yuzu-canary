// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cinttypes>
#include "common/common_funcs.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/file_sys/content_archive.h"
#include "core/gdbstub/gdbstub.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/resource_limit.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/deconstructed_rom_directory.h"
#include "core/loader/nso.h"
#include "core/memory.h"

namespace Loader {

static std::string FindRomFS(const std::string& directory) {
    std::string filepath_romfs;
    const auto callback = [&filepath_romfs](unsigned*, const std::string& directory,
                                            const std::string& virtual_name) -> bool {
        const std::string physical_name = directory + virtual_name;
        if (FileUtil::IsDirectory(physical_name)) {
            // Skip directories
            return true;
        }

        // Verify extension
        const std::string extension = physical_name.substr(physical_name.find_last_of(".") + 1);
        if (Common::ToLower(extension) != "romfs") {
            return true;
        }

        // Found it - we are done
        filepath_romfs = std::move(physical_name);
        return false;
    };

    // Search the specified directory recursively, looking for the first .romfs file, which will
    // be used for the RomFS
    FileUtil::ForeachDirectoryEntry(nullptr, directory, callback);

    return filepath_romfs;
}

AppLoader_DeconstructedRomDirectory::AppLoader_DeconstructedRomDirectory(FileSys::VirtualFile file)
    : AppLoader(std::move(file)) {}

FileType AppLoader_DeconstructedRomDirectory::IdentifyType(const FileSys::VirtualFile& file) {
    if (FileSys::IsDirectoryExeFS(file->GetContainingDirectory())) {
        return FileType::DeconstructedRomDirectory;
    }

    return FileType::Error;
}

ResultStatus AppLoader_DeconstructedRomDirectory::Load(
    Kernel::SharedPtr<Kernel::Process>& process) {
    if (is_loaded) {
        return ResultStatus::ErrorAlreadyLoaded;
    }

    const FileSys::VirtualDir dir = file->GetContainingDirectory();
    const FileSys::VirtualFile npdm = dir->GetFile("main.npdm");
    if (npdm == nullptr)
        return ResultStatus::ErrorInvalidFormat;

    ResultStatus result = metadata.Load(npdm);
    if (result != ResultStatus::Success) {
        return result;
    }
    metadata.Print();

    const FileSys::ProgramAddressSpaceType arch_bits{metadata.GetAddressSpaceType()};
    if (arch_bits == FileSys::ProgramAddressSpaceType::Is32Bit) {
        return ResultStatus::ErrorUnsupportedArch;
    }

    // Load NSO modules
    VAddr next_load_addr{Memory::PROCESS_IMAGE_VADDR};
    for (const auto& module : {"rtld", "main", "subsdk0", "subsdk1", "subsdk2", "subsdk3",
                               "subsdk4", "subsdk5", "subsdk6", "subsdk7", "sdk"}) {
        const VAddr load_addr = next_load_addr;
        const FileSys::VirtualFile module_file = dir->GetFile(module);
        if (module_file != nullptr)
            next_load_addr = AppLoader_NSO::LoadModule(module_file, load_addr);
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

    // Find the RomFS by searching for a ".romfs" file in this directory
    const auto& files = dir->GetFiles();
    const auto romfs_iter =
        std::find_if(files.begin(), files.end(), [](const FileSys::VirtualFile& file) {
            return file->GetName().find(".romfs") != std::string::npos;
        });

    // Register the RomFS if a ".romfs" file was found
    if (romfs_iter != files.end() && *romfs_iter != nullptr) {
        romfs = *romfs_iter;
        Service::FileSystem::RegisterRomFS(std::make_unique<FileSys::RomFSFactory>(*this));
    }

    is_loaded = true;
    return ResultStatus::Success;
}

ResultStatus AppLoader_DeconstructedRomDirectory::ReadRomFS(FileSys::VirtualFile& dir) {
    if (romfs == nullptr)
        return ResultStatus::ErrorNotUsed;
    dir = romfs;
    return ResultStatus::Success;
}

} // namespace Loader
