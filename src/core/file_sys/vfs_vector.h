// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/file_sys/vfs.h"

namespace FileSys {

// An implementation of VfsFile that is backed by a vector optionally supplied upon construction
class VectorVfsFile : public VfsFile {
public:
    explicit VectorVfsFile(std::vector<u8> initial_data = {}, std::string name = "",
                           VirtualDir parent = nullptr);
    ~VectorVfsFile() override;

    std::string GetName() const override;
    size_t GetSize() const override;
    bool Resize(size_t new_size) override;
    std::shared_ptr<VfsDirectory> GetContainingDirectory() const override;
    bool IsWritable() const override;
    bool IsReadable() const override;
    size_t Read(u8* data, size_t length, size_t offset) const override;
    size_t Write(const u8* data, size_t length, size_t offset) override;
    bool Rename(std::string_view name) override;

    virtual void Assign(std::vector<u8> new_data);

private:
    std::vector<u8> data;
    VirtualDir parent;
    std::string name;
};

// An implementation of VfsDirectory that maintains two vectors for subdirectories and files.
// Vector data is supplied upon construction.
class VectorVfsDirectory : public VfsDirectory {
public:
    explicit VectorVfsDirectory(std::vector<VirtualFile> files = {},
                                std::vector<VirtualDir> dirs = {}, std::string name = "",
                                VirtualDir parent = nullptr);
    ~VectorVfsDirectory() override;

    std::vector<std::shared_ptr<VfsFile>> GetFiles() const override;
    std::vector<std::shared_ptr<VfsDirectory>> GetSubdirectories() const override;
    bool IsWritable() const override;
    bool IsReadable() const override;
    std::string GetName() const override;
    std::shared_ptr<VfsDirectory> GetParentDirectory() const override;
    bool DeleteSubdirectory(std::string_view name) override;
    bool DeleteFile(std::string_view name) override;
    bool Rename(std::string_view name) override;
    std::shared_ptr<VfsDirectory> CreateSubdirectory(std::string_view name) override;
    std::shared_ptr<VfsFile> CreateFile(std::string_view name) override;

    virtual void AddFile(VirtualFile file);
    virtual void AddDirectory(VirtualDir dir);

protected:
    bool ReplaceFileWithSubdirectory(VirtualFile file, VirtualDir dir) override;

private:
    std::vector<VirtualFile> files;
    std::vector<VirtualDir> dirs;

    VirtualDir parent;
    std::string name;
};

} // namespace FileSys
