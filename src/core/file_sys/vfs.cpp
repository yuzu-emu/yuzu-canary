// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <numeric>
#include "common/file_util.h"
#include "common/logging/backend.h"
#include "core/file_sys/vfs.h"

namespace FileSys {

VfsFile::~VfsFile() = default;

std::string VfsFile::GetExtension() const {
    return FileUtil::GetExtensionFromFilename(GetName());
}

VfsDirectory::~VfsDirectory() = default;

boost::optional<u8> VfsFile::ReadByte(size_t offset) const {
    u8 out{};
    size_t size = Read(&out, 1, offset);
    if (size == 1)
        return out;

    return boost::none;
}

std::vector<u8> VfsFile::ReadBytes(size_t size, size_t offset) const {
    std::vector<u8> out(size);
    size_t read_size = Read(out.data(), size, offset);
    out.resize(read_size);
    return out;
}

std::vector<u8> VfsFile::ReadAllBytes() const {
    return ReadBytes(GetSize());
}

bool VfsFile::WriteByte(u8 data, size_t offset) {
    return Write(&data, 1, offset) == 1;
}

size_t VfsFile::WriteBytes(std::vector<u8> data, size_t offset) {
    return Write(data.data(), data.size(), offset);
}

std::shared_ptr<VfsFile> VfsDirectory::GetFileRelative(const std::string& path) const {
    auto vec = FileUtil::SplitPathComponents(path);
    vec.erase(std::remove_if(vec.begin(), vec.end(), [](const auto& str) { return str.empty(); }),
              vec.end());
    if (vec.empty())
        return nullptr;
    if (vec.size() == 1)
        return GetFile(vec[0]);
    auto dir = GetSubdirectory(vec[0]);
    for (size_t component = 1; component < vec.size() - 1; ++component) {
        if (dir == nullptr)
            return nullptr;
        dir = dir->GetSubdirectory(vec[component]);
    }
    if (dir == nullptr)
        return nullptr;
    return dir->GetFile(vec.back());
}

std::shared_ptr<VfsFile> VfsDirectory::GetFileAbsolute(const std::string& path) const {
    if (IsRoot())
        return GetFileRelative(path);

    return GetParentDirectory()->GetFileAbsolute(path);
}

std::shared_ptr<VfsDirectory> VfsDirectory::GetDirectoryRelative(const std::string& path) const {
    auto vec = FileUtil::SplitPathComponents(path);
    vec.erase(std::remove_if(vec.begin(), vec.end(), [](const auto& str) { return str.empty(); }),
              vec.end());
    if (vec.empty())
        // TODO(DarkLordZach): Return this directory if path is '/' or similar. Can't currently
        // because of const-ness
        return nullptr;
    auto dir = GetSubdirectory(vec[0]);
    for (size_t component = 1; component < vec.size(); ++component) {
        if (dir == nullptr)
            return nullptr;
        dir = dir->GetSubdirectory(vec[component]);
    }
    return dir;
}

std::shared_ptr<VfsDirectory> VfsDirectory::GetDirectoryAbsolute(const std::string& path) const {
    if (IsRoot())
        return GetDirectoryRelative(path);

    return GetParentDirectory()->GetDirectoryAbsolute(path);
}

std::shared_ptr<VfsFile> VfsDirectory::GetFile(const std::string& name) const {
    const auto& files = GetFiles();
    const auto iter = std::find_if(files.begin(), files.end(),
                                   [&name](const auto& file1) { return name == file1->GetName(); });
    return iter == files.end() ? nullptr : *iter;
}

std::shared_ptr<VfsDirectory> VfsDirectory::GetSubdirectory(const std::string& name) const {
    const auto& subs = GetSubdirectories();
    const auto iter = std::find_if(subs.begin(), subs.end(),
                                   [&name](const auto& file1) { return name == file1->GetName(); });
    return iter == subs.end() ? nullptr : *iter;
}

bool VfsDirectory::IsRoot() const {
    return GetParentDirectory() == nullptr;
}

size_t VfsDirectory::GetSize() const {
    const auto& files = GetFiles();
    const auto file_total =
        std::accumulate(files.begin(), files.end(), 0ull,
                        [](const auto& f1, const auto& f2) { return f1 + f2->GetSize(); });

    const auto& sub_dir = GetSubdirectories();
    const auto subdir_total =
        std::accumulate(sub_dir.begin(), sub_dir.end(), 0ull,
                        [](const auto& f1, const auto& f2) { return f1 + f2->GetSize(); });

    return file_total + subdir_total;
}

std::shared_ptr<VfsFile> VfsDirectory::CreateFileRelative(const std::string& path) {
    auto vec = FileUtil::SplitPathComponents(path);
    vec.erase(std::remove_if(vec.begin(), vec.end(), [](const auto& str) { return str.empty(); }),
              vec.end());
    if (vec.empty())
        return nullptr;
    if (vec.size() == 1)
        return CreateFile(vec[0]);
    auto dir = GetSubdirectory(vec[0]);
    if (dir == nullptr) {
        dir = CreateSubdirectory(vec[0]);
        if (dir == nullptr)
            return nullptr;
    }

    return dir->CreateFileRelative(FileUtil::GetPathWithoutTop(path));
}

std::shared_ptr<VfsFile> VfsDirectory::CreateFileAbsolute(const std::string& path) {
    if (IsRoot())
        return CreateFileRelative(path);
    return GetParentDirectory()->CreateFileAbsolute(path);
}

std::shared_ptr<VfsDirectory> VfsDirectory::CreateDirectoryRelative(const std::string& path) {
    auto vec = FileUtil::SplitPathComponents(path);
    vec.erase(std::remove_if(vec.begin(), vec.end(), [](const auto& str) { return str.empty(); }),
              vec.end());
    if (vec.empty())
        return nullptr;
    if (vec.size() == 1)
        return CreateSubdirectory(vec[0]);
    auto dir = GetSubdirectory(vec[0]);
    if (dir == nullptr) {
        dir = CreateSubdirectory(vec[0]);
        if (dir == nullptr)
            return nullptr;
    }
    return dir->CreateDirectoryRelative(FileUtil::GetPathWithoutTop(path));
}

std::shared_ptr<VfsDirectory> VfsDirectory::CreateDirectoryAbsolute(const std::string& path) {
    if (IsRoot())
        return CreateDirectoryRelative(path);
    return GetParentDirectory()->CreateDirectoryAbsolute(path);
}

bool VfsDirectory::DeleteSubdirectoryRecursive(const std::string& name) {
    auto dir = GetSubdirectory(name);
    if (dir == nullptr)
        return false;

    bool success = true;
    for (const auto& file : dir->GetFiles()) {
        if (!DeleteFile(file->GetName()))
            success = false;
    }

    for (const auto& sdir : dir->GetSubdirectories()) {
        if (!dir->DeleteSubdirectoryRecursive(sdir->GetName()))
            success = false;
    }

    return success;
}

bool VfsDirectory::Copy(const std::string& src, const std::string& dest) {
    const auto f1 = GetFile(src);
    auto f2 = CreateFile(dest);
    if (f1 == nullptr || f2 == nullptr)
        return false;

    if (!f2->Resize(f1->GetSize())) {
        DeleteFile(dest);
        return false;
    }

    return f2->WriteBytes(f1->ReadAllBytes()) == f1->GetSize();
}

bool ReadOnlyVfsDirectory::IsWritable() const {
    return false;
}

bool ReadOnlyVfsDirectory::IsReadable() const {
    return true;
}

std::shared_ptr<VfsDirectory> ReadOnlyVfsDirectory::CreateSubdirectory(const std::string& name) {
    return nullptr;
}

std::shared_ptr<VfsFile> ReadOnlyVfsDirectory::CreateFile(const std::string& name) {
    return nullptr;
}

bool ReadOnlyVfsDirectory::DeleteSubdirectory(const std::string& name) {
    return false;
}

bool ReadOnlyVfsDirectory::DeleteFile(const std::string& name) {
    return false;
}

bool ReadOnlyVfsDirectory::Rename(const std::string& name) {
    return false;
}
} // namespace FileSys
