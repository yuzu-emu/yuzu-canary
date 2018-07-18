// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/common_paths.h"
#include "common/logging/log.h"
#include "core/file_sys/vfs_real.h"

namespace FileSys {

static std::string PermissionsToCharArray(Mode perms) {
    std::string out;
    switch (perms) {
    case Mode::Read:
        out += "r";
        break;
    case Mode::Write:
        out += "r+";
        break;
    case Mode::Append:
        out += "a";
        break;
    }
    return out + "b";
}

RealVfsFile::RealVfsFile(const std::string& path_, Mode perms_)
    : backing(path_, PermissionsToCharArray(perms_).c_str()), path(path_),
      parent_path(FileUtil::GetParentPath(path_)),
      path_components(FileUtil::SplitPathComponents(path_)),
      parent_components(FileUtil::SliceVector(path_components, 0, path_components.size() - 1)),
      perms(perms_) {}

std::string RealVfsFile::GetName() const {
    return path_components.back();
}

size_t RealVfsFile::GetSize() const {
    return backing.GetSize();
}

bool RealVfsFile::Resize(size_t new_size) {
    return backing.Resize(new_size);
}

std::shared_ptr<VfsDirectory> RealVfsFile::GetContainingDirectory() const {
    return std::make_shared<RealVfsDirectory>(parent_path, perms);
}

bool RealVfsFile::IsWritable() const {
    return perms == Mode::Append || perms == Mode::Write;
}

bool RealVfsFile::IsReadable() const {
    return perms == Mode::Read || perms == Mode::Write;
}

size_t RealVfsFile::Read(u8* data, size_t length, size_t offset) const {
    if (!backing.Seek(offset, SEEK_SET))
        return 0;
    return backing.ReadBytes(data, length);
}

size_t RealVfsFile::Write(const u8* data, size_t length, size_t offset) {
    if (!backing.Seek(offset, SEEK_SET))
        return 0;
    return backing.WriteBytes(data, length);
}

bool RealVfsFile::Rename(const std::string& name) {
    const auto out = FileUtil::Rename(GetName(), name);
    path = parent_path + DIR_SEP + name;
    path_components = parent_components;
    path_components.push_back(name);
    backing = FileUtil::IOFile(path, PermissionsToCharArray(perms).c_str());
    return out;
}

bool RealVfsFile::Close() {
    return backing.Close();
}

RealVfsDirectory::RealVfsDirectory(const std::string& path_, Mode perms_)
    : path(FileUtil::RemoveTrailingSlash(path_)), parent_path(FileUtil::GetParentPath(path)),
      path_components(FileUtil::SplitPathComponents(path)),
      parent_components(FileUtil::SliceVector(path_components, 0, path_components.size() - 1)),
      perms(perms_) {
    if (!FileUtil::Exists(path) && (perms == Mode::Write || perms == Mode::Append))
        FileUtil::CreateDir(path);
    unsigned size;
    if (perms == Mode::Append)
        return;

    FileUtil::ForeachDirectoryEntry(
        &size, path,
        [this](unsigned* entries_out, const std::string& directory, const std::string& filename) {
            std::string full_path = directory + DIR_SEP + filename;
            if (FileUtil::IsDirectory(full_path))
                subdirectories.emplace_back(std::make_shared<RealVfsDirectory>(full_path, perms));
            else
                files.emplace_back(std::make_shared<RealVfsFile>(full_path, perms));
            return true;
        });
}

std::vector<std::shared_ptr<VfsFile>> RealVfsDirectory::GetFiles() const {
    return std::vector<std::shared_ptr<VfsFile>>(files);
}

std::vector<std::shared_ptr<VfsDirectory>> RealVfsDirectory::GetSubdirectories() const {
    return std::vector<std::shared_ptr<VfsDirectory>>(subdirectories);
}

bool RealVfsDirectory::IsWritable() const {
    return perms == Mode::Write || perms == Mode::Append;
}

bool RealVfsDirectory::IsReadable() const {
    return perms == Mode::Read || perms == Mode::Write;
}

std::string RealVfsDirectory::GetName() const {
    return path_components.back();
}

std::shared_ptr<VfsDirectory> RealVfsDirectory::GetParentDirectory() const {
    if (path_components.size() <= 1)
        return nullptr;

    return std::make_shared<RealVfsDirectory>(parent_path, perms);
}

std::shared_ptr<VfsDirectory> RealVfsDirectory::CreateSubdirectory(const std::string& name) {
    if (!FileUtil::CreateDir(path + DIR_SEP + name))
        return nullptr;
    subdirectories.emplace_back(std::make_shared<RealVfsDirectory>(path + DIR_SEP + name, perms));
    return subdirectories.back();
}

std::shared_ptr<VfsFile> RealVfsDirectory::CreateFile(const std::string& name) {
    if (!FileUtil::CreateEmptyFile(path + DIR_SEP + name))
        return nullptr;
    files.emplace_back(std::make_shared<RealVfsFile>(path + DIR_SEP + name, perms));
    return files.back();
}

bool RealVfsDirectory::DeleteSubdirectory(const std::string& name) {
    return FileUtil::DeleteDirRecursively(path + DIR_SEP + name);
}

bool RealVfsDirectory::DeleteFile(const std::string& name) {
    auto file = GetFile(name);
    if (file == nullptr)
        return false;
    files.erase(std::find(files.begin(), files.end(), file));
    auto real_file = std::static_pointer_cast<RealVfsFile>(file);
    real_file->Close();
    return FileUtil::Delete(path + DIR_SEP + name);
}

bool RealVfsDirectory::Rename(const std::string& name) {
    return FileUtil::Rename(path, parent_path + DIR_SEP + name);
}

bool RealVfsDirectory::ReplaceFileWithSubdirectory(VirtualFile file, VirtualDir dir) {
    auto iter = std::find(files.begin(), files.end(), file);
    if (iter == files.end())
        return false;

    files[iter - files.begin()] = files.back();
    files.pop_back();

    subdirectories.emplace_back(dir);

    return true;
}
} // namespace FileSys
