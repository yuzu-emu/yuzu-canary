// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <utility>

#include "common/assert.h"
#include "common/file_util.h"
#include "core/core.h"
#include "core/file_sys/bis_factory.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/mode.h"
#include "core/file_sys/romfs_factory.h"
#include "core/file_sys/savedata_factory.h"
#include "core/file_sys/sdmc_factory.h"
#include "core/file_sys/vfs.h"
#include "core/file_sys/vfs_offset.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/filesystem/fsp_ldr.h"
#include "core/hle/service/filesystem/fsp_pr.h"
#include "core/hle/service/filesystem/fsp_srv.h"
#include "filesystem.h"

namespace Service::FileSystem {

// Size of emulated sd card free space, reported in bytes.
// Just using 32GB because thats reasonable
// TODO(DarkLordZach): Eventually make this configurable in settings.
constexpr u64 EMULATED_SD_REPORTED_SIZE = 32000000000;

static FileSys::VirtualDir GetDirectoryRelativeWrapped(FileSys::VirtualDir base,
                                                       std::string_view dir_name_) {
    std::string dir_name(FileUtil::SanitizePath(dir_name_));
    if (dir_name.empty() || dir_name == "." || dir_name == "/" || dir_name == "\\")
        return base;

    return base->GetDirectoryRelative(dir_name);
}

VfsDirectoryServiceWrapper::VfsDirectoryServiceWrapper(FileSys::VirtualDir backing_)
    : backing(std::move(backing_)) {}

std::string VfsDirectoryServiceWrapper::GetName() const {
    return backing->GetName();
}

ResultCode VfsDirectoryServiceWrapper::CreateFile(const std::string& path_, u64 size) const {
    std::string path(FileUtil::SanitizePath(path_));
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

ResultCode VfsDirectoryServiceWrapper::DeleteFile(const std::string& path_) const {
    std::string path(FileUtil::SanitizePath(path_));
    if (path.empty()) {
        // TODO(DarkLordZach): Why do games call this and what should it do? Works as is but...
        return RESULT_SUCCESS;
    }

    auto dir = GetDirectoryRelativeWrapped(backing, FileUtil::GetParentPath(path));
    if (dir->GetFile(FileUtil::GetFilename(path)) == nullptr) {
        return FileSys::ERROR_PATH_NOT_FOUND;
    }
    if (!dir->DeleteFile(FileUtil::GetFilename(path))) {
        // TODO(DarkLordZach): Find a better error code for this
        return ResultCode(-1);
    }

    return RESULT_SUCCESS;
}

ResultCode VfsDirectoryServiceWrapper::CreateDirectory(const std::string& path_) const {
    std::string path(FileUtil::SanitizePath(path_));
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

ResultCode VfsDirectoryServiceWrapper::DeleteDirectory(const std::string& path_) const {
    std::string path(FileUtil::SanitizePath(path_));
    auto dir = GetDirectoryRelativeWrapped(backing, FileUtil::GetParentPath(path));
    if (!dir->DeleteSubdirectory(FileUtil::GetFilename(path))) {
        // TODO(DarkLordZach): Find a better error code for this
        return ResultCode(-1);
    }
    return RESULT_SUCCESS;
}

ResultCode VfsDirectoryServiceWrapper::DeleteDirectoryRecursively(const std::string& path_) const {
    std::string path(FileUtil::SanitizePath(path_));
    auto dir = GetDirectoryRelativeWrapped(backing, FileUtil::GetParentPath(path));
    if (!dir->DeleteSubdirectoryRecursive(FileUtil::GetFilename(path))) {
        // TODO(DarkLordZach): Find a better error code for this
        return ResultCode(-1);
    }
    return RESULT_SUCCESS;
}

ResultCode VfsDirectoryServiceWrapper::RenameFile(const std::string& src_path_,
                                                  const std::string& dest_path_) const {
    std::string src_path(FileUtil::SanitizePath(src_path_));
    std::string dest_path(FileUtil::SanitizePath(dest_path_));
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

ResultCode VfsDirectoryServiceWrapper::RenameDirectory(const std::string& src_path_,
                                                       const std::string& dest_path_) const {
    std::string src_path(FileUtil::SanitizePath(src_path_));
    std::string dest_path(FileUtil::SanitizePath(dest_path_));
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

ResultVal<FileSys::VirtualFile> VfsDirectoryServiceWrapper::OpenFile(const std::string& path_,
                                                                     FileSys::Mode mode) const {
    std::string path(FileUtil::SanitizePath(path_));
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

ResultVal<FileSys::VirtualDir> VfsDirectoryServiceWrapper::OpenDirectory(const std::string& path_) {
    std::string path(FileUtil::SanitizePath(path_));
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
    const std::string& path_) const {
    std::string path(FileUtil::SanitizePath(path_));
    auto dir = GetDirectoryRelativeWrapped(backing, FileUtil::GetParentPath(path));
    if (dir == nullptr)
        return FileSys::ERROR_PATH_NOT_FOUND;
    auto filename = FileUtil::GetFilename(path);
    // TODO(Subv): Some games use the '/' path, find out what this means.
    if (filename.empty())
        return MakeResult(FileSys::EntryType::Directory);

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
static std::unique_ptr<FileSys::BISFactory> bis_factory;

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

ResultCode RegisterBIS(std::unique_ptr<FileSys::BISFactory>&& factory) {
    ASSERT_MSG(bis_factory == nullptr, "Tried to register a second BIS");
    bis_factory = std::move(factory);
    LOG_DEBUG(Service_FS, "Registered BIS");
    return RESULT_SUCCESS;
}

ResultVal<FileSys::VirtualFile> OpenRomFSCurrentProcess() {
    LOG_TRACE(Service_FS, "Opening RomFS for current process");

    if (romfs_factory == nullptr) {
        // TODO(bunnei): Find a better error code for this
        return ResultCode(-1);
    }

    return romfs_factory->OpenCurrentProcess();
}

ResultVal<FileSys::VirtualFile> OpenRomFS(u64 title_id, FileSys::StorageId storage_id,
                                          FileSys::ContentRecordType type) {
    LOG_TRACE(Service_FS, "Opening RomFS for title_id={:016X}, storage_id={:02X}, type={:02X}",
              title_id, static_cast<u8>(storage_id), static_cast<u8>(type));

    if (romfs_factory == nullptr) {
        // TODO(bunnei): Find a better error code for this
        return ResultCode(-1);
    }

    return romfs_factory->Open(title_id, storage_id, type);
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

std::shared_ptr<FileSys::RegisteredCacheUnion> GetUnionContents() {
    return std::make_shared<FileSys::RegisteredCacheUnion>(
        std::vector<std::shared_ptr<FileSys::RegisteredCache>>{
            GetSystemNANDContents(), GetUserNANDContents(), GetSDMCContents()});
}

std::shared_ptr<FileSys::RegisteredCache> GetSystemNANDContents() {
    LOG_TRACE(Service_FS, "Opening System NAND Contents");

    if (bis_factory == nullptr)
        return nullptr;

    return bis_factory->GetSystemNANDContents();
}

std::shared_ptr<FileSys::RegisteredCache> GetUserNANDContents() {
    LOG_TRACE(Service_FS, "Opening User NAND Contents");

    if (bis_factory == nullptr)
        return nullptr;

    return bis_factory->GetUserNANDContents();
}

std::shared_ptr<FileSys::RegisteredCache> GetSDMCContents() {
    LOG_TRACE(Service_FS, "Opening SDMC Contents");

    if (sdmc_factory == nullptr)
        return nullptr;

    return sdmc_factory->GetSDMCContents();
}

void CreateFactories(const FileSys::VirtualFilesystem& vfs, bool overwrite) {
    if (overwrite) {
        bis_factory = nullptr;
        save_data_factory = nullptr;
        sdmc_factory = nullptr;
    }

    auto nand_directory = vfs->OpenDirectory(FileUtil::GetUserPath(FileUtil::UserPath::NANDDir),
                                             FileSys::Mode::ReadWrite);
    auto sd_directory = vfs->OpenDirectory(FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir),
                                           FileSys::Mode::ReadWrite);

    if (bis_factory == nullptr)
        bis_factory = std::make_unique<FileSys::BISFactory>(nand_directory);
    if (save_data_factory == nullptr)
        save_data_factory = std::make_unique<FileSys::SaveDataFactory>(std::move(nand_directory));
    if (sdmc_factory == nullptr)
        sdmc_factory = std::make_unique<FileSys::SDMCFactory>(std::move(sd_directory));
}

void InstallInterfaces(SM::ServiceManager& service_manager, const FileSys::VirtualFilesystem& vfs) {
    romfs_factory = nullptr;
    CreateFactories(vfs, false);
    std::make_shared<FSP_LDR>()->InstallAsService(service_manager);
    std::make_shared<FSP_PR>()->InstallAsService(service_manager);
    std::make_shared<FSP_SRV>()->InstallAsService(service_manager);
}

} // namespace Service::FileSystem
