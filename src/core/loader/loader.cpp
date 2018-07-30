// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <string>
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/file_sys/vfs_real.h"
#include "core/hle/kernel/process.h"
#include "core/loader/deconstructed_rom_directory.h"
#include "core/loader/elf.h"
#include "core/loader/nca.h"
#include "core/loader/nro.h"
#include "core/loader/nso.h"
#include "core/loader/xci.h"

namespace Loader {

const std::initializer_list<Kernel::AddressMapping> default_address_mappings = {
    {0x1FF50000, 0x8000, true},    // part of DSP RAM
    {0x1FF70000, 0x8000, true},    // part of DSP RAM
    {0x1F000000, 0x600000, false}, // entire VRAM
};

FileType IdentifyFile(FileSys::VirtualFile file) {
    FileType type;

#define CHECK_TYPE(loader)                                                                         \
    type = AppLoader_##loader::IdentifyType(file);                                                 \
    if (FileType::Error != type)                                                                   \
        return type;

    CHECK_TYPE(DeconstructedRomDirectory)
    CHECK_TYPE(ELF)
    CHECK_TYPE(NSO)
    CHECK_TYPE(NRO)
    CHECK_TYPE(NCA)
    CHECK_TYPE(XCI)

#undef CHECK_TYPE

    return FileType::Unknown;
}

FileType IdentifyFile(const std::string& file_name) {
    return IdentifyFile(std::make_shared<FileSys::RealVfsFile>(file_name));
}

FileType GuessFromFilename(const std::string& name) {
    if (name == "main")
        return FileType::DeconstructedRomDirectory;

    const std::string extension =
        Common::ToLower(std::string(FileUtil::GetExtensionFromFilename(name)));

    if (extension == "elf")
        return FileType::ELF;
    if (extension == "nro")
        return FileType::NRO;
    if (extension == "nso")
        return FileType::NSO;
    if (extension == "nca")
        return FileType::NCA;
    if (extension == "xci")
        return FileType::XCI;

    return FileType::Unknown;
}

const char* GetFileTypeString(FileType type) {
    switch (type) {
    case FileType::ELF:
        return "ELF";
    case FileType::NRO:
        return "NRO";
    case FileType::NSO:
        return "NSO";
    case FileType::NCA:
        return "NCA";
    case FileType::XCI:
        return "XCI";
    case FileType::DeconstructedRomDirectory:
        return "Directory";
    case FileType::Error:
    case FileType::Unknown:
        break;
    }

    return "unknown";
}

/**
 * Get a loader for a file with a specific type
 * @param file The file to load
 * @param type The type of the file
 * @param file the file to retrieve the loader for
 * @param type the file type
 * @return std::unique_ptr<AppLoader> a pointer to a loader object;  nullptr for unsupported type
 */
static std::unique_ptr<AppLoader> GetFileLoader(FileSys::VirtualFile file, FileType type) {
    switch (type) {

    // Standard ELF file format.
    case FileType::ELF:
        return std::make_unique<AppLoader_ELF>(std::move(file));

    // NX NSO file format.
    case FileType::NSO:
        return std::make_unique<AppLoader_NSO>(std::move(file));

    // NX NRO file format.
    case FileType::NRO:
        return std::make_unique<AppLoader_NRO>(std::move(file));

    // NX NCA file format.
    case FileType::NCA:
        return std::make_unique<AppLoader_NCA>(std::move(file));

    case FileType::XCI:
        return std::make_unique<AppLoader_XCI>(std::move(file));

    // NX deconstructed ROM directory.
    case FileType::DeconstructedRomDirectory:
        return std::make_unique<AppLoader_DeconstructedRomDirectory>(std::move(file));

    default:
        return nullptr;
    }
}

std::unique_ptr<AppLoader> GetLoader(FileSys::VirtualFile file) {
    FileType type = IdentifyFile(file);
    FileType filename_type = GuessFromFilename(file->GetName());

    if (type != filename_type) {
        LOG_WARNING(Loader, "File {} has a different type than its extension.", file->GetName());
        if (FileType::Unknown == type)
            type = filename_type;
    }

    LOG_DEBUG(Loader, "Loading file {} as {}...", file->GetName(), GetFileTypeString(type));

    return GetFileLoader(std::move(file), type);
}

} // namespace Loader
