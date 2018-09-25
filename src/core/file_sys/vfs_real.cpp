// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>
#include "common/assert.h"
#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "core/file_sys/vfs_real.h"

namespace FileSys {

static std::string ModeFlagsToString(Mode mode) {
    std::string mode_str;

    // Calculate the correct open mode for the file.
    if (mode & Mode::Read && mode & Mode::Write) {
        if (mode & Mode::Append)
            mode_str = "a+";
        else
            mode_str = "r+";
    } else {
        if (mode & Mode::Read)
            mode_str = "r";
        else if (mode & Mode::Append)
            mode_str = "a";
        else if (mode & Mode::Write)
            mode_str = "w";
        else
            UNREACHABLE_MSG("Invalid file open mode: {:02X}", static_cast<u8>(mode));
    }

    mode_str += "b";

    return mode_str;
}

RealVfsFilesystem::RealVfsFilesystem() : VfsFilesystem(nullptr) {}
RealVfsFilesystem::~RealVfsFilesystem() = default;

std::string RealVfsFilesystem::GetName() const {
    return "Real";
}

bool RealVfsFilesystem::IsReadable() const {
    return true;
}

bool RealVfsFilesystem::IsWritable() const {
    return true;
}

VfsEntryType RealVfsFilesystem::GetEntryType(std::string_view path_) const {
    const auto path = FileUtil::SanitizePath(path_, FileUtil::DirectorySeparator::PlatformDefault);
    if (!FileUtil::Exists(path))
        return VfsEntryType::None;
    if (FileUtil::IsDirectory(path))
        return VfsEntryType::Directory;

    return VfsEntryType::File;
}

VirtualFile RealVfsFilesystem::OpenFile(std::string_view path_, Mode perms) {
    const auto path = FileUtil::SanitizePath(path_, FileUtil::DirectorySeparator::PlatformDefault);
    if (cache.find(path) != cache.end()) {
        auto weak = cache[path];
        if (!weak.expired()) {
            return std::shared_ptr<RealVfsFile>(new RealVfsFile(*this, weak.lock(), path, perms));
        }
    }

    if (!FileUtil::Exists(path) && (perms & Mode::WriteAppend) != 0)
        FileUtil::CreateEmptyFile(path);

    auto backing = std::make_shared<FileUtil::IOFile>(path, ModeFlagsToString(perms).c_str());
    cache[path] = backing;

    // Cannot use make_shared as RealVfsFile constructor is private
    return std::shared_ptr<RealVfsFile>(new RealVfsFile(*this, backing, path, perms));
}

VirtualFile RealVfsFilesystem::CreateFile(std::string_view path_, Mode perms) {
    const auto path = FileUtil::SanitizePath(path_, FileUtil::DirectorySeparator::PlatformDefault);
    const auto path_fwd = FileUtil::SanitizePath(path, FileUtil::DirectorySeparator::ForwardSlash);
    if (!FileUtil::Exists(path)) {
        FileUtil::CreateFullPath(path_fwd);
        if (!FileUtil::CreateEmptyFile(path))
            return nullptr;
    }
    return OpenFile(path, perms);
}

VirtualFile RealVfsFilesystem::CopyFile(std::string_view old_path_, std::string_view new_path_) {
    const auto old_path =
        FileUtil::SanitizePath(old_path_, FileUtil::DirectorySeparator::PlatformDefault);
    const auto new_path =
        FileUtil::SanitizePath(new_path_, FileUtil::DirectorySeparator::PlatformDefault);

    if (!FileUtil::Exists(old_path) || FileUtil::Exists(new_path) ||
        FileUtil::IsDirectory(old_path) || !FileUtil::Copy(old_path, new_path))
        return nullptr;
    return OpenFile(new_path, Mode::ReadWrite);
}

VirtualFile RealVfsFilesystem::MoveFile(std::string_view old_path_, std::string_view new_path_) {
    const auto old_path =
        FileUtil::SanitizePath(old_path_, FileUtil::DirectorySeparator::PlatformDefault);
    const auto new_path =
        FileUtil::SanitizePath(new_path_, FileUtil::DirectorySeparator::PlatformDefault);

    if (!FileUtil::Exists(old_path) || FileUtil::Exists(new_path) ||
        FileUtil::IsDirectory(old_path) || !FileUtil::Rename(old_path, new_path))
        return nullptr;

    if (cache.find(old_path) != cache.end()) {
        auto cached = cache[old_path];
        if (!cached.expired()) {
            auto file = cached.lock();
            file->Open(new_path, "r+b");
            cache.erase(old_path);
            cache[new_path] = file;
        }
    }
    return OpenFile(new_path, Mode::ReadWrite);
}

bool RealVfsFilesystem::DeleteFile(std::string_view path_) {
    const auto path = FileUtil::SanitizePath(path_, FileUtil::DirectorySeparator::PlatformDefault);
    if (cache.find(path) != cache.end()) {
        if (!cache[path].expired())
            cache[path].lock()->Close();
        cache.erase(path);
    }
    return FileUtil::Delete(path);
}

VirtualDir RealVfsFilesystem::OpenDirectory(std::string_view path_, Mode perms) {
    const auto path = FileUtil::SanitizePath(path_, FileUtil::DirectorySeparator::PlatformDefault);
    // Cannot use make_shared as RealVfsDirectory constructor is private
    return std::shared_ptr<RealVfsDirectory>(new RealVfsDirectory(*this, path, perms));
}

VirtualDir RealVfsFilesystem::CreateDirectory(std::string_view path_, Mode perms) {
    const auto path = FileUtil::SanitizePath(path_, FileUtil::DirectorySeparator::PlatformDefault);
    const auto path_fwd = FileUtil::SanitizePath(path, FileUtil::DirectorySeparator::ForwardSlash);
    if (!FileUtil::Exists(path)) {
        FileUtil::CreateFullPath(path_fwd);
        if (!FileUtil::CreateDir(path))
            return nullptr;
    }
    // Cannot use make_shared as RealVfsDirectory constructor is private
    return std::shared_ptr<RealVfsDirectory>(new RealVfsDirectory(*this, path, perms));
}

VirtualDir RealVfsFilesystem::CopyDirectory(std::string_view old_path_,
                                            std::string_view new_path_) {
    const auto old_path =
        FileUtil::SanitizePath(old_path_, FileUtil::DirectorySeparator::PlatformDefault);
    const auto new_path =
        FileUtil::SanitizePath(new_path_, FileUtil::DirectorySeparator::PlatformDefault);
    if (!FileUtil::Exists(old_path) || FileUtil::Exists(new_path) ||
        !FileUtil::IsDirectory(old_path))
        return nullptr;
    FileUtil::CopyDir(old_path, new_path);
    return OpenDirectory(new_path, Mode::ReadWrite);
}

VirtualDir RealVfsFilesystem::MoveDirectory(std::string_view old_path_,
                                            std::string_view new_path_) {
    const auto old_path =
        FileUtil::SanitizePath(old_path_, FileUtil::DirectorySeparator::PlatformDefault);
    const auto new_path =
        FileUtil::SanitizePath(new_path_, FileUtil::DirectorySeparator::PlatformDefault);
    if (!FileUtil::Exists(old_path) || FileUtil::Exists(new_path) ||
        FileUtil::IsDirectory(old_path) || !FileUtil::Rename(old_path, new_path))
        return nullptr;

    for (auto& kv : cache) {
        // Path in cache starts with old_path
        if (kv.first.rfind(old_path, 0) == 0) {
            const auto file_old_path =
                FileUtil::SanitizePath(kv.first, FileUtil::DirectorySeparator::PlatformDefault);
            const auto file_new_path =
                FileUtil::SanitizePath(new_path + DIR_SEP + kv.first.substr(old_path.size()),
                                       FileUtil::DirectorySeparator::PlatformDefault);
            auto cached = cache[file_old_path];
            if (!cached.expired()) {
                auto file = cached.lock();
                file->Open(file_new_path, "r+b");
                cache.erase(file_old_path);
                cache[file_new_path] = file;
            }
        }
    }

    return OpenDirectory(new_path, Mode::ReadWrite);
}

bool RealVfsFilesystem::DeleteDirectory(std::string_view path_) {
    const auto path = FileUtil::SanitizePath(path_, FileUtil::DirectorySeparator::PlatformDefault);
    for (auto& kv : cache) {
        // Path in cache starts with old_path
        if (kv.first.rfind(path, 0) == 0) {
            if (!cache[kv.first].expired())
                cache[kv.first].lock()->Close();
            cache.erase(kv.first);
        }
    }
    return FileUtil::DeleteDirRecursively(path);
}

RealVfsFile::RealVfsFile(RealVfsFilesystem& base_, std::shared_ptr<FileUtil::IOFile> backing_,
                         const std::string& path_, Mode perms_)
    : base(base_), backing(std::move(backing_)), path(path_),
      parent_path(FileUtil::GetParentPath(path_)),
      path_components(FileUtil::SplitPathComponents(path_)),
      parent_components(FileUtil::SliceVector(path_components, 0, path_components.size() - 1)),
      perms(perms_) {}

RealVfsFile::~RealVfsFile() = default;

std::string RealVfsFile::GetName() const {
    return path_components.back();
}

std::size_t RealVfsFile::GetSize() const {
    return backing->GetSize();
}

bool RealVfsFile::Resize(std::size_t new_size) {
    return backing->Resize(new_size);
}

std::shared_ptr<VfsDirectory> RealVfsFile::GetContainingDirectory() const {
    return base.OpenDirectory(parent_path, perms);
}

bool RealVfsFile::IsWritable() const {
    return (perms & Mode::WriteAppend) != 0;
}

bool RealVfsFile::IsReadable() const {
    return (perms & Mode::ReadWrite) != 0;
}

std::size_t RealVfsFile::Read(u8* data, std::size_t length, std::size_t offset) const {
    if (!backing->Seek(offset, SEEK_SET))
        return 0;
    return backing->ReadBytes(data, length);
}

std::size_t RealVfsFile::Write(const u8* data, std::size_t length, std::size_t offset) {
    if (!backing->Seek(offset, SEEK_SET))
        return 0;
    return backing->WriteBytes(data, length);
}

bool RealVfsFile::Rename(std::string_view name) {
    return base.MoveFile(path, parent_path + DIR_SEP + std::string(name)) != nullptr;
}

bool RealVfsFile::Close() {
    return backing->Close();
}

// TODO(DarkLordZach): MSVC would not let me combine the following two functions using 'if
// constexpr' because there is a compile error in the branch not used.

template <>
std::vector<VirtualFile> RealVfsDirectory::IterateEntries<RealVfsFile, VfsFile>() const {
    if (perms == Mode::Append)
        return {};

    std::vector<VirtualFile> out;
    FileUtil::ForeachDirectoryEntry(
        nullptr, path,
        [&out, this](u64* entries_out, const std::string& directory, const std::string& filename) {
            const std::string full_path = directory + DIR_SEP + filename;
            if (!FileUtil::IsDirectory(full_path))
                out.emplace_back(base.OpenFile(full_path, perms));
            return true;
        });

    return out;
}

template <>
std::vector<VirtualDir> RealVfsDirectory::IterateEntries<RealVfsDirectory, VfsDirectory>() const {
    if (perms == Mode::Append)
        return {};

    std::vector<VirtualDir> out;
    FileUtil::ForeachDirectoryEntry(
        nullptr, path,
        [&out, this](u64* entries_out, const std::string& directory, const std::string& filename) {
            const std::string full_path = directory + DIR_SEP + filename;
            if (FileUtil::IsDirectory(full_path))
                out.emplace_back(base.OpenDirectory(full_path, perms));
            return true;
        });

    return out;
}

RealVfsDirectory::RealVfsDirectory(RealVfsFilesystem& base_, const std::string& path_, Mode perms_)
    : base(base_), path(FileUtil::RemoveTrailingSlash(path_)),
      parent_path(FileUtil::GetParentPath(path)),
      path_components(FileUtil::SplitPathComponents(path)),
      parent_components(FileUtil::SliceVector(path_components, 0, path_components.size() - 1)),
      perms(perms_) {
    if (!FileUtil::Exists(path) && perms & Mode::WriteAppend)
        FileUtil::CreateDir(path);
}

RealVfsDirectory::~RealVfsDirectory() = default;

std::shared_ptr<VfsFile> RealVfsDirectory::GetFileRelative(std::string_view path) const {
    const auto full_path = FileUtil::SanitizePath(this->path + DIR_SEP + std::string(path));
    if (!FileUtil::Exists(full_path) || FileUtil::IsDirectory(full_path))
        return nullptr;
    return base.OpenFile(full_path, perms);
}

std::shared_ptr<VfsDirectory> RealVfsDirectory::GetDirectoryRelative(std::string_view path) const {
    const auto full_path = FileUtil::SanitizePath(this->path + DIR_SEP + std::string(path));
    if (!FileUtil::Exists(full_path) || !FileUtil::IsDirectory(full_path))
        return nullptr;
    return base.OpenDirectory(full_path, perms);
}

std::shared_ptr<VfsFile> RealVfsDirectory::GetFile(std::string_view name) const {
    return GetFileRelative(name);
}

std::shared_ptr<VfsDirectory> RealVfsDirectory::GetSubdirectory(std::string_view name) const {
    return GetDirectoryRelative(name);
}

std::shared_ptr<VfsFile> RealVfsDirectory::CreateFileRelative(std::string_view path) {
    const auto full_path = FileUtil::SanitizePath(this->path + DIR_SEP + std::string(path));
    return base.CreateFile(full_path, perms);
}

std::shared_ptr<VfsDirectory> RealVfsDirectory::CreateDirectoryRelative(std::string_view path) {
    const auto full_path = FileUtil::SanitizePath(this->path + DIR_SEP + std::string(path));
    return base.CreateDirectory(full_path, perms);
}

bool RealVfsDirectory::DeleteSubdirectoryRecursive(std::string_view name) {
    auto full_path = FileUtil::SanitizePath(this->path + DIR_SEP + std::string(name));
    return base.DeleteDirectory(full_path);
}

std::vector<std::shared_ptr<VfsFile>> RealVfsDirectory::GetFiles() const {
    return IterateEntries<RealVfsFile, VfsFile>();
}

std::vector<std::shared_ptr<VfsDirectory>> RealVfsDirectory::GetSubdirectories() const {
    return IterateEntries<RealVfsDirectory, VfsDirectory>();
}

bool RealVfsDirectory::IsWritable() const {
    return (perms & Mode::WriteAppend) != 0;
}

bool RealVfsDirectory::IsReadable() const {
    return (perms & Mode::ReadWrite) != 0;
}

std::string RealVfsDirectory::GetName() const {
    return path_components.back();
}

std::shared_ptr<VfsDirectory> RealVfsDirectory::GetParentDirectory() const {
    if (path_components.size() <= 1)
        return nullptr;

    return base.OpenDirectory(parent_path, perms);
}

std::shared_ptr<VfsDirectory> RealVfsDirectory::CreateSubdirectory(std::string_view name) {
    const std::string subdir_path = (path + DIR_SEP).append(name);
    return base.CreateDirectory(subdir_path, perms);
}

std::shared_ptr<VfsFile> RealVfsDirectory::CreateFile(std::string_view name) {
    const std::string file_path = (path + DIR_SEP).append(name);
    return base.CreateFile(file_path, perms);
}

bool RealVfsDirectory::DeleteSubdirectory(std::string_view name) {
    const std::string subdir_path = (path + DIR_SEP).append(name);
    return base.DeleteDirectory(subdir_path);
}

bool RealVfsDirectory::DeleteFile(std::string_view name) {
    const std::string file_path = (path + DIR_SEP).append(name);
    return base.DeleteFile(file_path);
}

bool RealVfsDirectory::Rename(std::string_view name) {
    const std::string new_name = (parent_path + DIR_SEP).append(name);
    return base.MoveFile(path, new_name) != nullptr;
}

std::string RealVfsDirectory::GetFullPath() const {
    auto out = path;
    std::replace(out.begin(), out.end(), '\\', '/');
    return out;
}

std::map<std::string, VfsEntryType, std::less<>> RealVfsDirectory::GetEntries() const {
    if (perms == Mode::Append)
        return {};

    std::map<std::string, VfsEntryType, std::less<>> out;
    FileUtil::ForeachDirectoryEntry(
        nullptr, path,
        [&out](u64* entries_out, const std::string& directory, const std::string& filename) {
            const std::string full_path = directory + DIR_SEP + filename;
            out.emplace(filename, FileUtil::IsDirectory(full_path) ? VfsEntryType::Directory
                                                                   : VfsEntryType::File);
            return true;
        });

    return out;
}

bool RealVfsDirectory::ReplaceFileWithSubdirectory(VirtualFile file, VirtualDir dir) {
    return false;
}
} // namespace FileSys
