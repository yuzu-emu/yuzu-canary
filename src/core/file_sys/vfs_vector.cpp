// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <utility>
#include "core/file_sys/vfs_vector.h"

namespace FileSys {
VectorVfsDirectory::VectorVfsDirectory(std::vector<VirtualFile> files_,
                                       std::vector<VirtualDir> dirs_, std::string name_,
                                       VirtualDir parent_)
    : files(std::move(files_)), dirs(std::move(dirs_)), parent(std::move(parent_)),
      name(std::move(name_)) {}

std::vector<std::shared_ptr<VfsFile>> VectorVfsDirectory::GetFiles() const {
    return files;
}

std::vector<std::shared_ptr<VfsDirectory>> VectorVfsDirectory::GetSubdirectories() const {
    return dirs;
}

bool VectorVfsDirectory::IsWritable() const {
    return false;
}

bool VectorVfsDirectory::IsReadable() const {
    return true;
}

std::string VectorVfsDirectory::GetName() const {
    return name;
}

std::shared_ptr<VfsDirectory> VectorVfsDirectory::GetParentDirectory() const {
    return parent;
}

template <typename T>
static bool FindAndRemoveVectorElement(std::vector<T>& vec, std::string_view name) {
    const auto iter =
        std::find_if(vec.begin(), vec.end(), [name](const T& e) { return e->GetName() == name; });
    if (iter == vec.end())
        return false;

    vec.erase(iter);
    return true;
}

bool VectorVfsDirectory::DeleteSubdirectory(std::string_view name) {
    return FindAndRemoveVectorElement(dirs, name);
}

bool VectorVfsDirectory::DeleteFile(std::string_view name) {
    return FindAndRemoveVectorElement(files, name);
}

bool VectorVfsDirectory::Rename(std::string_view name_) {
    name = name_;
    return true;
}

std::shared_ptr<VfsDirectory> VectorVfsDirectory::CreateSubdirectory(std::string_view name) {
    return nullptr;
}

std::shared_ptr<VfsFile> VectorVfsDirectory::CreateFile(std::string_view name) {
    return nullptr;
}

void VectorVfsDirectory::AddFile(VirtualFile file) {
    files.push_back(std::move(file));
}

void VectorVfsDirectory::AddDirectory(VirtualDir dir) {
    dirs.push_back(std::move(dir));
}

bool VectorVfsDirectory::ReplaceFileWithSubdirectory(VirtualFile file, VirtualDir dir) {
    if (!DeleteFile(file->GetName()))
        return false;
    dirs.emplace_back(std::move(dir));
    return true;
}
} // namespace FileSys
