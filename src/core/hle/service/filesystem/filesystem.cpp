// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma optimize("", off)

#include "common/assert.h"
#include "common/file_util.h"
#include "core/core.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/savedata_factory.h"
#include "core/file_sys/sdmc_factory.h"
#include "core/file_sys/vfs.h"
#include "core/file_sys/vfs_offset.h"
#include "core/file_sys/vfs_real.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/filesystem/fsp_srv.h"

namespace Service::FileSystem {

// Size of emulated sd card free space, reported in bytes.
// Just using 32GB because thats reasonable
// TODO(DarkLordZach): Eventually make this configurable in settings.
constexpr u64 EMULATED_SD_REPORTED_SIZE = 32000000000;

static FileSys::VirtualDir GetDirectoryRelativeWrapped(FileSys::VirtualDir base,
                                                       const std::string& dir_name) {
    if (dir_name == "." || dir_name == "" || dir_name == "/" || dir_name == "\\")
        return base;

    return base->GetDirectoryRelative(dir_name);
}

VfsDirectoryServiceWrapper::VfsDirectoryServiceWrapper(FileSys::VirtualDir backing_)
    : backing(backing_) {}

std::string VfsDirectoryServiceWrapper::GetName() const {
    return backing->GetName();
}

ResultCode VfsDirectoryServiceWrapper::CreateFile(const std::string& path, u64 size) const {
    auto dir = GetDirectoryRelativeWrapped(backing, FileUtil::GetParentPath(path));
    auto file = dir->CreateFile(FileUtil::GetFilename(path));
    if (file == nullptr) {
        // TODO(DarkLordZach): Find a better error code for this
        return ResultCode(-1);
    }
    if (!file->Resize(size)) {
        // TODO(DarkLordZach): Find a better error code for this
        return ResultCode(-1);
    }
    return RESULT_SUCCESS;
}

ResultCode VfsDirectoryServiceWrapper::DeleteFile(const std::string& path) const {
    auto dir = GetDirectoryRelativeWrapped(backing, FileUtil::GetParentPath(path));
    if (path == "/" || path == "\\") {
        // TODO(DarkLordZach): Why do games call this and what should it do? Works as is but...
        return RESULT_SUCCESS;
    }
    if (dir->GetFile(FileUtil::GetFilename(path)) == nullptr)
        return FileSys::ERROR_PATH_NOT_FOUND;
    if (!backing->DeleteFile(FileUtil::GetFilename(path))) {
        // TODO(DarkLordZach): Find a better error code for this
        return ResultCode(-1);
    }
    return RESULT_SUCCESS;
}

ResultCode VfsDirectoryServiceWrapper::CreateDirectory(const std::string& path) const {
    auto dir = GetDirectoryRelativeWrapped(backing, FileUtil::GetParentPath(path));
    if (dir == nullptr && FileUtil::GetFilename(FileUtil::GetParentPath(path)).empty())
        dir = backing;
    auto new_dir = dir->CreateSubdirectory(FileUtil::GetFilename(path));
    if (new_dir == nullptr) {
        // TODO(DarkLordZach): Find a better error code for this
        return ResultCode(-1);
    }
    return RESULT_SUCCESS;
}

ResultCode VfsDirectoryServiceWrapper::DeleteDirectory(const std::string& path) const {
    auto dir = GetDirectoryRelativeWrapped(backing, FileUtil::GetParentPath(path));
    if (!dir->DeleteSubdirectory(FileUtil::GetFilename(path))) {
        // TODO(DarkLordZach): Find a better error code for this
        return ResultCode(-1);
    }
    return RESULT_SUCCESS;
}

ResultCode VfsDirectoryServiceWrapper::DeleteDirectoryRecursively(const std::string& path) const {
    auto dir = GetDirectoryRelativeWrapped(backing, FileUtil::GetParentPath(path));
    if (!dir->DeleteSubdirectoryRecursive(FileUtil::GetFilename(path))) {
        // TODO(DarkLordZach): Find a better error code for this
        return ResultCode(-1);
    }
    return RESULT_SUCCESS;
}

ResultCode VfsDirectoryServiceWrapper::RenameFile(const std::string& src_path,
                                                  const std::string& dest_path) const {
    auto src = backing->GetFileRelative(src_path);
    if (FileUtil::GetParentPath(src_path) == FileUtil::GetParentPath(dest_path)) {
        // Use more-optimized vfs implementation rename.
        if (src == nullptr)
            return FileSys::ERROR_PATH_NOT_FOUND;
        if (!src->Rename(FileUtil::GetFilename(dest_path))) {
            // TODO(DarkLordZach): Find a better error code for this
            return ResultCode(-1);
        }
        return RESULT_SUCCESS;
    }

    // Move by hand -- TODO(DarkLordZach): Optimize
    auto c_res = CreateFile(dest_path, src->GetSize());
    if (c_res != RESULT_SUCCESS)
        return c_res;

    auto dest = backing->GetFileRelative(dest_path);
    ASSERT_MSG(dest != nullptr, "Newly created file with success cannot be found.");

    ASSERT_MSG(dest->WriteBytes(src->ReadAllBytes()) == src->GetSize(),
               "Could not write all of the bytes but everything else has succeded.");

    if (!src->GetContainingDirectory()->DeleteFile(FileUtil::GetFilename(src_path))) {
        // TODO(DarkLordZach): Find a better error code for this
        return ResultCode(-1);
    }

    return RESULT_SUCCESS;
}

ResultCode VfsDirectoryServiceWrapper::RenameDirectory(const std::string& src_path,
                                                       const std::string& dest_path) const {
    auto src = GetDirectoryRelativeWrapped(backing, src_path);
    if (FileUtil::GetParentPath(src_path) == FileUtil::GetParentPath(dest_path)) {
        // Use more-optimized vfs implementation rename.
        if (src == nullptr)
            return FileSys::ERROR_PATH_NOT_FOUND;
        if (!src->Rename(FileUtil::GetFilename(dest_path))) {
            // TODO(DarkLordZach): Find a better error code for this
            return ResultCode(-1);
        }
        return RESULT_SUCCESS;
    }

    // TODO(DarkLordZach): Implement renaming across the tree (move).
    ASSERT_MSG(false,
               "Could not rename directory with path \"{}\" to new path \"{}\" because parent dirs "
               "don't match -- UNIMPLEMENTED",
               src_path, dest_path);

    // TODO(DarkLordZach): Find a better error code for this
    return ResultCode(-1);
}

ResultVal<FileSys::VirtualFile> VfsDirectoryServiceWrapper::OpenFile(const std::string& path,
                                                                     FileSys::Mode mode) const {
    auto npath = path;
    while (npath.size() > 0 && (npath[0] == '/' || npath[0] == '\\'))
        npath = npath.substr(1);
    auto file = backing->GetFileRelative(npath);
    if (file == nullptr)
        return FileSys::ERROR_PATH_NOT_FOUND;

    if (mode == FileSys::Mode::Append) {
        return MakeResult<FileSys::VirtualFile>(
            std::make_shared<FileSys::OffsetVfsFile>(file, 0, file->GetSize()));
    }

    return MakeResult<FileSys::VirtualFile>(file);
}

ResultVal<FileSys::VirtualDir> VfsDirectoryServiceWrapper::OpenDirectory(const std::string& path) {
    auto dir = GetDirectoryRelativeWrapped(backing, path);
    if (dir == nullptr) {
        // TODO(DarkLordZach): Find a better error code for this
        return ResultCode(-1);
    }
    return MakeResult(dir);
}

u64 VfsDirectoryServiceWrapper::GetFreeSpaceSize() const {
    if (backing->IsWritable())
        return EMULATED_SD_REPORTED_SIZE;

    return 0;
}

ResultVal<FileSys::EntryType> VfsDirectoryServiceWrapper::GetEntryType(
    const std::string& path) const {
    auto dir = GetDirectoryRelativeWrapped(backing, FileUtil::GetParentPath(path));
    if (dir == nullptr)
        return FileSys::ERROR_PATH_NOT_FOUND;
    auto filename = FileUtil::GetFilename(path);
    if (dir->GetFile(filename) != nullptr)
        return MakeResult(FileSys::EntryType::File);
    if (dir->GetSubdirectory(filename) != nullptr)
        return MakeResult(FileSys::EntryType::Directory);
    return FileSys::ERROR_PATH_NOT_FOUND;
}

/**
 * Map of registered file systems, identified by type. Once an file system is registered here, it
 * is never removed until UnregisterFileSystems is called.
 */
static std::unique_ptr<FileSys::RomFSFactory> romfs_factory;
static std::unique_ptr<FileSys::SaveDataFactory> save_data_factory;
static std::unique_ptr<FileSys::SDMCFactory> sdmc_factory;

ResultCode RegisterRomFS(std::unique_ptr<FileSys::RomFSFactory>&& factory) {
    ASSERT_MSG(romfs_factory == nullptr, "Tried to register a second RomFS");
    romfs_factory = std::move(factory);
    LOG_DEBUG(Service_FS, "Registered RomFS");
    return RESULT_SUCCESS;
}

ResultCode RegisterSaveData(std::unique_ptr<FileSys::SaveDataFactory>&& factory) {
    ASSERT_MSG(romfs_factory == nullptr, "Tried to register a second save data");
    save_data_factory = std::move(factory);
    LOG_DEBUG(Service_FS, "Registered save data");
    return RESULT_SUCCESS;
}

ResultCode RegisterSDMC(std::unique_ptr<FileSys::SDMCFactory>&& factory) {
    ASSERT_MSG(sdmc_factory == nullptr, "Tried to register a second SDMC");
    sdmc_factory = std::move(factory);
    LOG_DEBUG(Service_FS, "Registered SDMC");
    return RESULT_SUCCESS;
}

ResultVal<FileSys::VirtualFile> OpenRomFS(u64 title_id) {
    LOG_TRACE(Service_FS, "Opening RomFS for title_id={:016X}", title_id);

    if (romfs_factory == nullptr) {
        // TODO(bunnei): Find a better error code for this
        return ResultCode(-1);
    }

    return romfs_factory->Open(title_id);
}

ResultVal<FileSys::VirtualDir> OpenSaveData(FileSys::SaveDataSpaceId space,
                                            FileSys::SaveDataDescriptor save_struct) {
    LOG_TRACE(Service_FS, "Opening Save Data for space_id={:01X}, save_struct={}",
              static_cast<u8>(space), save_struct.DebugInfo());

    if (save_data_factory == nullptr) {
        return ResultCode(ErrorModule::FS, FileSys::ErrCodes::TitleNotFound);
    }

    return save_data_factory->Open(space, save_struct);
}

ResultVal<FileSys::VirtualDir> OpenSDMC() {
    LOG_TRACE(Service_FS, "Opening SDMC");

    if (sdmc_factory == nullptr) {
        return ResultCode(ErrorModule::FS, FileSys::ErrCodes::SdCardNotFound);
    }

    return sdmc_factory->Open();
}

void RegisterFileSystems() {
    romfs_factory = nullptr;
    save_data_factory = nullptr;
    sdmc_factory = nullptr;

    auto nand_directory = std::make_shared<FileSys::RealVfsDirectory>(
        FileUtil::GetUserPath(D_NAND_IDX), FileSys::Mode::Write);
    auto sd_directory = std::make_shared<FileSys::RealVfsDirectory>(
        FileUtil::GetUserPath(D_SDMC_IDX), FileSys::Mode::Write);

    auto savedata = std::make_unique<FileSys::SaveDataFactory>(std::move(nand_directory));
    save_data_factory = std::move(savedata);

    auto sdcard = std::make_unique<FileSys::SDMCFactory>(std::move(sd_directory));
    sdmc_factory = std::move(sdcard);
}

void InstallInterfaces(SM::ServiceManager& service_manager) {
    RegisterFileSystems();
    std::make_shared<FSP_SRV>()->InstallAsService(service_manager);
}

} // namespace Service::FileSystem
