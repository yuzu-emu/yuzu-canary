// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/file_util.h"
#include "core/file_sys/mode.h"
#include "core/file_sys/vfs.h"

namespace FileSys {

// An implmentation of VfsFile that represents a file on the user's computer.
struct RealVfsFile : public VfsFile {
    friend struct RealVfsDirectory;

    RealVfsFile(const std::string& name, Mode perms = Mode::Read);

    std::string GetName() const override;
    size_t GetSize() const override;
    bool Resize(size_t new_size) override;
    std::shared_ptr<VfsDirectory> GetContainingDirectory() const override;
    bool IsWritable() const override;
    bool IsReadable() const override;
    size_t Read(u8* data, size_t length, size_t offset) const override;
    size_t Write(const u8* data, size_t length, size_t offset) override;
    bool Rename(const std::string& name) override;

private:
    bool Close();

    FileUtil::IOFile backing;
    std::string path;
    std::string parent_path;
    std::vector<std::string> path_components;
    std::vector<std::string> parent_components;
    Mode perms;
};

// An implementation of VfsDirectory that represents a directory on the user's computer.
struct RealVfsDirectory : public VfsDirectory {
    RealVfsDirectory(const std::string& path, Mode perms);

    std::vector<std::shared_ptr<VfsFile>> GetFiles() const override;
    std::vector<std::shared_ptr<VfsDirectory>> GetSubdirectories() const override;
    bool IsWritable() const override;
    bool IsReadable() const override;
    std::string GetName() const override;
    std::shared_ptr<VfsDirectory> GetParentDirectory() const override;
    std::shared_ptr<VfsDirectory> CreateSubdirectory(const std::string& name) override;
    std::shared_ptr<VfsFile> CreateFile(const std::string& name) override;
    bool DeleteSubdirectory(const std::string& name) override;
    bool DeleteFile(const std::string& name) override;
    bool Rename(const std::string& name) override;

protected:
    bool ReplaceFileWithSubdirectory(VirtualFile file, VirtualDir dir) override;

private:
    std::string path;
    std::string parent_path;
    std::vector<std::string> path_components;
    std::vector<std::string> parent_components;
    Mode perms;
    std::vector<std::shared_ptr<VfsFile>> files;
    std::vector<std::shared_ptr<VfsDirectory>> subdirectories;
};

} // namespace FileSys
