// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <string>
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/hle/kernel/process.h"
#include "core/loader/deconstructed_rom_directory.h"
#include "core/loader/elf.h"
#include "core/loader/nca.h"
#include "core/loader/nro.h"
#include "core/loader/nso.h"

namespace Loader {

const std::initializer_list<Kernel::AddressMapping> default_address_mappings = {
    {0x1FF50000, 0x8000, true},    // part of DSP RAM
    {0x1FF70000, 0x8000, true},    // part of DSP RAM
    {0x1F000000, 0x600000, false}, // entire VRAM
};

FileType IdentifyFile(FileUtil::IOFile& file, const std::string& filepath) {
    FileType type;

#define CHECK_TYPE(loader)                                                                         \
    type = AppLoader_##loader::IdentifyType(file, filepath);                                       \
    if (FileType::Error != type)                                                                   \
        return type;

    CHECK_TYPE(DeconstructedRomDirectory)
    CHECK_TYPE(ELF)
    CHECK_TYPE(NSO)
    CHECK_TYPE(NRO)
    CHECK_TYPE(NCA)

#undef CHECK_TYPE

    return FileType::Unknown;
}

FileType IdentifyFile(const std::string& file_name) {
    FileUtil::IOFile file(file_name, "rb");
    if (!file.IsOpen()) {
        NGLOG_ERROR(Loader, "Failed to load file {}", file_name);
        return FileType::Unknown;
    }

    return IdentifyFile(file, file_name);
}

FileType GuessFromExtension(const std::string& extension_) {
    std::string extension = Common::ToLower(extension_);

    if (extension == ".elf")
        return FileType::ELF;
    else if (extension == ".nro")
        return FileType::NRO;
    else if (extension == ".nso")
        return FileType::NSO;
    else if (extension == ".nca")
        return FileType::NCA;

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
 * @param filename the file name (without path)
 * @param filepath the file full path (with name)
 * @return std::unique_ptr<AppLoader> a pointer to a loader object;  nullptr for unsupported type
 */
static std::unique_ptr<AppLoader> GetFileLoader(FileUtil::IOFile&& file, FileType type,
                                                const std::string& filename,
                                                const std::string& filepath) {
    switch (type) {

    // Standard ELF file format.
    case FileType::ELF:
        return std::make_unique<AppLoader_ELF>(std::move(file), filename);

    // NX NSO file format.
    case FileType::NSO:
        return std::make_unique<AppLoader_NSO>(std::move(file), filepath);

    // NX NRO file format.
    case FileType::NRO:
        return std::make_unique<AppLoader_NRO>(std::move(file), filepath);

    // NX NCA file format.
    case FileType::NCA:
        return std::make_unique<AppLoader_NCA>(std::move(file), filepath);

    // NX deconstructed ROM directory.
    case FileType::DeconstructedRomDirectory:
        return std::make_unique<AppLoader_DeconstructedRomDirectory>(std::move(file), filepath);

    default:
        return nullptr;
    }
}

std::unique_ptr<AppLoader> GetLoader(const std::string& filename) {
    FileUtil::IOFile file(filename, "rb");
    if (!file.IsOpen()) {
        NGLOG_ERROR(Loader, "Failed to load file {}", filename);
        return nullptr;
    }

    std::string filename_filename, filename_extension;
    Common::SplitPath(filename, nullptr, &filename_filename, &filename_extension);

    FileType type = IdentifyFile(file, filename);
    FileType filename_type = GuessFromExtension(filename_extension);

    if (type != filename_type) {
        NGLOG_WARNING(Loader, "File {} has a different type than its extension.", filename);
        if (FileType::Unknown == type)
            type = filename_type;
    }

    NGLOG_DEBUG(Loader, "Loading file {} as {}...", filename, GetFileTypeString(type));

    return GetFileLoader(std::move(file), type, filename_filename, filename);
}

} // namespace Loader
