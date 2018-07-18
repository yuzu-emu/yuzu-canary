// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include "common/common_types.h"
#include "core/file_sys/directory.h"
#include "core/file_sys/mode.h"
#include "core/file_sys/romfs_factory.h"
#include "core/file_sys/savedata_factory.h"
#include "core/file_sys/sdmc_factory.h"
#include "core/hle/result.h"

namespace Service {

namespace SM {
class ServiceManager;
} // namespace SM

namespace FileSystem {

ResultCode RegisterRomFS(std::unique_ptr<FileSys::RomFSFactory>&& factory);
ResultCode RegisterSaveData(std::unique_ptr<FileSys::SaveDataFactory>&& factory);
ResultCode RegisterSDMC(std::unique_ptr<FileSys::SDMCFactory>&& factory);

// TODO(DarkLordZach): BIS Filesystem
// ResultCode RegisterBIS(std::unique_ptr<FileSys::BISFactory>&& factory);
ResultVal<FileSys::VirtualFile> OpenRomFS(u64 title_id);
ResultVal<FileSys::VirtualDir> OpenSaveData(FileSys::SaveDataSpaceId space,
                                            FileSys::SaveDataDescriptor save_struct);
ResultVal<FileSys::VirtualDir> OpenSDMC();

// TODO(DarkLordZach): BIS Filesystem
// ResultVal<std::unique_ptr<FileSys::FileSystemBackend>> OpenBIS();

/// Registers all Filesystem services with the specified service manager.
void InstallInterfaces(SM::ServiceManager& service_manager);

// A class that wraps a VfsDirectory with methods that return ResultVal and ResultCode instead of
// pointers and booleans. This makes using a VfsDirectory with switch services much easier and
// avoids repetitive code.
class VfsDirectoryServiceWrapper {
public:
    explicit VfsDirectoryServiceWrapper(FileSys::VirtualDir backing);

    /**
     * Get a descriptive name for the archive (e.g. "RomFS", "SaveData", etc.)
     */
    std::string GetName() const;

    /**
     * Create a file specified by its path
     * @param path Path relative to the Archive
     * @param size The size of the new file, filled with zeroes
     * @return Result of the operation
     */
    ResultCode CreateFile(const std::string& path, u64 size) const;

    /**
     * Delete a file specified by its path
     * @param path Path relative to the archive
     * @return Result of the operation
     */
    ResultCode DeleteFile(const std::string& path) const;

    /**
     * Create a directory specified by its path
     * @param path Path relative to the archive
     * @return Result of the operation
     */
    ResultCode CreateDirectory(const std::string& path) const;

    /**
     * Delete a directory specified by its path
     * @param path Path relative to the archive
     * @return Result of the operation
     */
    ResultCode DeleteDirectory(const std::string& path) const;

    /**
     * Delete a directory specified by its path and anything under it
     * @param path Path relative to the archive
     * @return Result of the operation
     */
    ResultCode DeleteDirectoryRecursively(const std::string& path) const;

    /**
     * Rename a File specified by its path
     * @param src_path Source path relative to the archive
     * @param dest_path Destination path relative to the archive
     * @return Result of the operation
     */
    ResultCode RenameFile(const std::string& src_path, const std::string& dest_path) const;

    /**
     * Rename a Directory specified by its path
     * @param src_path Source path relative to the archive
     * @param dest_path Destination path relative to the archive
     * @return Result of the operation
     */
    ResultCode RenameDirectory(const std::string& src_path, const std::string& dest_path) const;

    /**
     * Open a file specified by its path, using the specified mode
     * @param path Path relative to the archive
     * @param mode Mode to open the file with
     * @return Opened file, or error code
     */
    ResultVal<FileSys::VirtualFile> OpenFile(const std::string& path, FileSys::Mode mode) const;

    /**
     * Open a directory specified by its path
     * @param path Path relative to the archive
     * @return Opened directory, or error code
     */
    ResultVal<FileSys::VirtualDir> OpenDirectory(const std::string& path);

    /**
     * Get the free space
     * @return The number of free bytes in the archive
     */
    u64 GetFreeSpaceSize() const;

    /**
     * Get the type of the specified path
     * @return The type of the specified path or error code
     */
    ResultVal<FileSys::EntryType> GetEntryType(const std::string& path) const;

private:
    FileSys::VirtualDir backing;
};

} // namespace FileSystem
} // namespace Service
