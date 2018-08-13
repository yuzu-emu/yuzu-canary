// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string_view>
#include <boost/container/flat_map.hpp>
#include "common/file_util.h"
#include "core/file_sys/mode.h"
#include "core/file_sys/vfs.h"

namespace FileSys {

class RealVfsFilesystem : public VfsFilesystem {
public:
    RealVfsFilesystem();

    std::string GetName() const override;
    bool IsReadable() const override;
    bool IsWritable() const override;
    VfsEntryType GetEntryType(std::string_view path) const override;
    VirtualFile OpenFile(std::string_view path, Mode perms = Mode::Read) override;
    VirtualFile CreateFile(std::string_view path, Mode perms = Mode::ReadWrite) override;
    VirtualFile CopyFile(std::string_view old_path, std::string_view new_path) override;
    VirtualFile MoveFile(std::string_view old_path, std::string_view new_path) override;
    bool DeleteFile(std::string_view path) override;
    VirtualDir OpenDirectory(std::string_view path, Mode perms = Mode::Read) override;
    VirtualDir CreateDirectory(std::string_view path, Mode perms = Mode::ReadWrite) override;
    VirtualDir CopyDirectory(std::string_view old_path, std::string_view new_path) override;
    VirtualDir MoveDirectory(std::string_view old_path, std::string_view new_path) override;
    bool DeleteDirectory(std::string_view path) override;

private:
    boost::container::flat_map<std::string, std::weak_ptr<FileUtil::IOFile>> cache;
};

// An implmentation of VfsFile that represents a file on the user's computer.
class RealVfsFile : public VfsFile {
    friend class RealVfsDirectory;
    friend class RealVfsFilesystem;

    RealVfsFile(RealVfsFilesystem& base, std::shared_ptr<FileUtil::IOFile> backing,
                const std::string& path, Mode perms = Mode::Read);

public:
    std::string GetName() const override;
    size_t GetSize() const override;
    bool Resize(size_t new_size) override;
    std::shared_ptr<VfsDirectory> GetContainingDirectory() const override;
    bool IsWritable() const override;
    bool IsReadable() const override;
    size_t Read(u8* data, size_t length, size_t offset) const override;
    size_t Write(const u8* data, size_t length, size_t offset) override;
    bool Rename(std::string_view name) override;

private:
    bool Close();

    RealVfsFilesystem& base;
    std::shared_ptr<FileUtil::IOFile> backing;
    std::string path;
    std::string parent_path;
    std::vector<std::string> path_components;
    std::vector<std::string> parent_components;
    Mode perms;
};

// An implementation of VfsDirectory that represents a directory on the user's computer.
class RealVfsDirectory : public VfsDirectory {
    friend class RealVfsFilesystem;

    RealVfsDirectory(RealVfsFilesystem& base, const std::string& path, Mode perms = Mode::Read);

public:
    std::shared_ptr<VfsFile> GetFileRelative(std::string_view path) const override;
    std::shared_ptr<VfsDirectory> GetDirectoryRelative(std::string_view path) const override;
    std::shared_ptr<VfsFile> GetFile(std::string_view name) const override;
    std::shared_ptr<VfsDirectory> GetSubdirectory(std::string_view name) const override;
    std::shared_ptr<VfsFile> CreateFileRelative(std::string_view path) override;
    std::shared_ptr<VfsDirectory> CreateDirectoryRelative(std::string_view path) override;
    bool DeleteSubdirectoryRecursive(std::string_view name) override;
    std::vector<std::shared_ptr<VfsFile>> GetFiles() const override;
    std::vector<std::shared_ptr<VfsDirectory>> GetSubdirectories() const override;
    bool IsWritable() const override;
    bool IsReadable() const override;
    std::string GetName() const override;
    std::shared_ptr<VfsDirectory> GetParentDirectory() const override;
    std::shared_ptr<VfsDirectory> CreateSubdirectory(std::string_view name) override;
    std::shared_ptr<VfsFile> CreateFile(std::string_view name) override;
    bool DeleteSubdirectory(std::string_view name) override;
    bool DeleteFile(std::string_view name) override;
    bool Rename(std::string_view name) override;
    std::string GetFullPath() const override;

protected:
    bool ReplaceFileWithSubdirectory(VirtualFile file, VirtualDir dir) override;

private:
    template <typename T, typename R>
    std::vector<std::shared_ptr<R>> IterateEntries() const;

    RealVfsFilesystem& base;
    std::string path;
    std::string parent_path;
    std::vector<std::string> path_components;
    std::vector<std::string> parent_components;
    Mode perms;
};

} // namespace FileSys
