// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <utility>
#include "core/file_sys/vfs_layered.h"

namespace FileSys {

VirtualDir LayerDirectories(std::vector<VirtualDir> dirs, std::string name) {
    if (dirs.empty())
        return nullptr;
    if (dirs.size() == 1)
        return dirs[0];

    return std::shared_ptr<VfsDirectory>(new LayeredVfsDirectory(std::move(dirs), std::move(name)));
}

LayeredVfsDirectory::LayeredVfsDirectory(std::vector<VirtualDir> dirs, std::string name)
    : dirs(std::move(dirs)), name(std::move(name)) {}

LayeredVfsDirectory::~LayeredVfsDirectory() = default;

std::shared_ptr<VfsFile> LayeredVfsDirectory::GetFileRelative(std::string_view path) const {
    for (const auto& layer : dirs) {
        const auto file = layer->GetFileRelative(path);
        if (file != nullptr)
            return file;
    }

    return nullptr;
}

std::shared_ptr<VfsDirectory> LayeredVfsDirectory::GetDirectoryRelative(
    std::string_view path) const {
    std::vector<VirtualDir> out;
    for (const auto& layer : dirs) {
        auto dir = layer->GetDirectoryRelative(path);
        if (dir != nullptr)
            out.push_back(std::move(dir));
    }

    return LayerDirectories(std::move(out));
}

std::shared_ptr<VfsFile> LayeredVfsDirectory::GetFile(std::string_view name) const {
    return GetFileRelative(name);
}

std::shared_ptr<VfsDirectory> LayeredVfsDirectory::GetSubdirectory(std::string_view name) const {
    return GetDirectoryRelative(name);
}

std::string LayeredVfsDirectory::GetFullPath() const {
    return dirs[0]->GetFullPath();
}

std::vector<std::shared_ptr<VfsFile>> LayeredVfsDirectory::GetFiles() const {
    std::vector<VirtualFile> out;
    for (const auto& layer : dirs) {
        for (const auto& file : layer->GetFiles()) {
            if (std::find_if(out.begin(), out.end(), [&file](const VirtualFile& comp) {
                    return comp->GetName() == file->GetName();
                }) == out.end()) {
                out.push_back(file);
            }
        }
    }

    return out;
}

std::vector<std::shared_ptr<VfsDirectory>> LayeredVfsDirectory::GetSubdirectories() const {
    std::vector<std::string> names;
    for (const auto& layer : dirs) {
        for (const auto& sd : layer->GetSubdirectories()) {
            if (std::find(names.begin(), names.end(), sd->GetName()) == names.end())
                names.push_back(sd->GetName());
        }
    }

    std::vector<VirtualDir> out;
    out.reserve(names.size());
    for (const auto& subdir : names)
        out.push_back(GetSubdirectory(subdir));

    return out;
}

bool LayeredVfsDirectory::IsWritable() const {
    return false;
}

bool LayeredVfsDirectory::IsReadable() const {
    return true;
}

std::string LayeredVfsDirectory::GetName() const {
    return name.empty() ? dirs[0]->GetName() : name;
}

std::shared_ptr<VfsDirectory> LayeredVfsDirectory::GetParentDirectory() const {
    return dirs[0]->GetParentDirectory();
}

std::shared_ptr<VfsDirectory> LayeredVfsDirectory::CreateSubdirectory(std::string_view name) {
    return nullptr;
}

std::shared_ptr<VfsFile> LayeredVfsDirectory::CreateFile(std::string_view name) {
    return nullptr;
}

bool LayeredVfsDirectory::DeleteSubdirectory(std::string_view name) {
    return false;
}

bool LayeredVfsDirectory::DeleteFile(std::string_view name) {
    return false;
}

bool LayeredVfsDirectory::Rename(std::string_view name_) {
    name = name_;
    return true;
}

bool LayeredVfsDirectory::ReplaceFileWithSubdirectory(VirtualFile file, VirtualDir dir) {
    return false;
}
} // namespace FileSys
