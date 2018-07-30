// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <boost/optional.hpp>
#include "common/common_types.h"
#include "common/file_util.h"
#include "core/file_sys/vfs.h"
#include "core/hle/kernel/kernel.h"

namespace Kernel {
struct AddressMapping;
class Process;
} // namespace Kernel

namespace Loader {

/// File types supported by CTR
enum class FileType {
    Error,
    Unknown,
    ELF,
    NSO,
    NRO,
    NCA,
    XCI,
    DeconstructedRomDirectory,
};

/**
 * Identifies the type of a bootable file based on the magic value in its header.
 * @param file open file
 * @return FileType of file
 */
FileType IdentifyFile(FileSys::VirtualFile file);

/**
 * Identifies the type of a bootable file based on the magic value in its header.
 * @param file_name path to file
 * @return FileType of file. Note: this will return FileType::Unknown if it is unable to determine
 * a filetype, and will never return FileType::Error.
 */
FileType IdentifyFile(const std::string& file_name);

/**
 * Guess the type of a bootable file from its name
 * @param name String name of bootable file
 * @return FileType of file. Note: this will return FileType::Unknown if it is unable to determine
 * a filetype, and will never return FileType::Error.
 */
FileType GuessFromFilename(const std::string& name);

/**
 * Convert a FileType into a string which can be displayed to the user.
 */
const char* GetFileTypeString(FileType type);

/// Return type for functions in Loader namespace
enum class ResultStatus {
    Success,
    Error,
    ErrorInvalidFormat,
    ErrorNotImplemented,
    ErrorNotLoaded,
    ErrorNotUsed,
    ErrorAlreadyLoaded,
    ErrorMemoryAllocationFailed,
    ErrorEncrypted,
    ErrorUnsupportedArch,
};

/// Interface for loading an application
class AppLoader : NonCopyable {
public:
    explicit AppLoader(FileSys::VirtualFile file) : file(std::move(file)) {}
    virtual ~AppLoader() {}

    /**
     * Returns the type of this file
     * @return FileType corresponding to the loaded file
     */
    virtual FileType GetFileType() = 0;

    /**
     * Load the application and return the created Process instance
     * @param process The newly created process.
     * @return The status result of the operation.
     */
    virtual ResultStatus Load(Kernel::SharedPtr<Kernel::Process>& process) = 0;

    /**
     * Loads the system mode that this application needs.
     * This function defaults to 2 (96MB allocated to the application) if it can't read the
     * information.
     * @returns A pair with the optional system mode, and and the status.
     */
    virtual std::pair<boost::optional<u32>, ResultStatus> LoadKernelSystemMode() {
        // 96MB allocated to the application.
        return std::make_pair(2, ResultStatus::Success);
    }

    /**
     * Get the code (typically .code section) of the application
     * @param buffer Reference to buffer to store data
     * @return ResultStatus result of function
     */
    virtual ResultStatus ReadCode(std::vector<u8>& buffer) {
        return ResultStatus::ErrorNotImplemented;
    }

    /**
     * Get the icon (typically icon section) of the application
     * @param buffer Reference to buffer to store data
     * @return ResultStatus result of function
     */
    virtual ResultStatus ReadIcon(std::vector<u8>& buffer) {
        return ResultStatus::ErrorNotImplemented;
    }

    /**
     * Get the banner (typically banner section) of the application
     * @param buffer Reference to buffer to store data
     * @return ResultStatus result of function
     */
    virtual ResultStatus ReadBanner(std::vector<u8>& buffer) {
        return ResultStatus::ErrorNotImplemented;
    }

    /**
     * Get the logo (typically logo section) of the application
     * @param buffer Reference to buffer to store data
     * @return ResultStatus result of function
     */
    virtual ResultStatus ReadLogo(std::vector<u8>& buffer) {
        return ResultStatus::ErrorNotImplemented;
    }

    /**
     * Get the program id of the application
     * @param out_program_id Reference to store program id into
     * @return ResultStatus result of function
     */
    virtual ResultStatus ReadProgramId(u64& out_program_id) {
        return ResultStatus::ErrorNotImplemented;
    }

    /**
     * Get the RomFS of the application
     * Since the RomFS can be huge, we return a file reference instead of copying to a buffer
     * @param dir The directory containing the RomFS
     * @return ResultStatus result of function
     */
    virtual ResultStatus ReadRomFS(FileSys::VirtualFile& dir) {
        return ResultStatus::ErrorNotImplemented;
    }

    /**
     * Get the update RomFS of the application
     * Since the RomFS can be huge, we return a file reference instead of copying to a buffer
     * @param file The file containing the RomFS
     * @return ResultStatus result of function
     */
    virtual ResultStatus ReadUpdateRomFS(FileSys::VirtualFile& file) {
        return ResultStatus::ErrorNotImplemented;
    }

    /**
     * Get the title of the application
     * @param title Reference to store the application title into
     * @return ResultStatus result of function
     */
    virtual ResultStatus ReadTitle(std::string& title) {
        return ResultStatus::ErrorNotImplemented;
    }

protected:
    FileSys::VirtualFile file;
    bool is_loaded = false;
};

/**
 * Common address mappings found in most games, used for binary formats that don't have this
 * information.
 */
extern const std::initializer_list<Kernel::AddressMapping> default_address_mappings;

/**
 * Identifies a bootable file and return a suitable loader
 * @param file The bootable file
 * @return the best loader for this file
 */
std::unique_ptr<AppLoader> GetLoader(FileSys::VirtualFile file);

} // namespace Loader
