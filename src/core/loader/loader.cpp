// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <ostream>
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

FileType GuessFromFilename(const std::string& name) {
    if (name == "main")
        return FileType::DeconstructedRomDirectory;
    if (name == "00")
        return FileType::NCA;

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

std::string GetFileTypeString(FileType type) {
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

constexpr std::array<const char*, 36> RESULT_MESSAGES{
    "The operation completed successfully.",
    "The loader requested to load is already loaded.",
    "The operation is not implemented.",
    "The loader is not initialized properly.",
    "The NPDM file has a bad header.",
    "The NPDM has a bad ACID header.",
    "The NPDM has a bad ACI header,",
    "The NPDM file has a bad file access control.",
    "The NPDM has a bad file access header.",
    "The PFS/HFS partition has a bad header.",
    "The PFS/HFS partition has incorrect size as determined by the header.",
    "The NCA file has a bad header.",
    "The general keyfile could not be found.",
    "The NCA Header key could not be found.",
    "The NCA Header key is incorrect or the header is invalid.",
    "Support for NCA2-type NCAs is not implemented.",
    "Support for NCA0-type NCAs is not implemented.",
    "The titlekey for this Rights ID could not be found.",
    "The titlekek for this crypto revision could not be found.",
    "The Rights ID in the header is invalid.",
    "The key area key for this application type and crypto revision could not be found.",
    "The key area key is incorrect or the section header is invalid.",
    "The titlekey and/or titlekek is incorrect or the section header is invalid.",
    "The XCI file is missing a Program-type NCA.",
    "The NCA file is not an application.",
    "The ExeFS partition could not be found.",
    "The XCI file has a bad header.",
    "The XCI file is missing a partition.",
    "The file could not be found or does not exist.",
    "The game is missing a program metadata file (main.npdm).",
    "The game uses the currently-unimplemented 32-bit architecture.",
    "The RomFS could not be found.",
    "The ELF file has incorrect size as determined by the header.",
    "There was a general error loading the NRO into emulated memory.",
    "There is no icon available.",
    "There is no control data available.",
};

std::ostream& operator<<(std::ostream& os, ResultStatus status) {
    os << RESULT_MESSAGES.at(static_cast<size_t>(status));
    return os;
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
