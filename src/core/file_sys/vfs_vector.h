// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/file_sys/vfs.h"

namespace FileSys {

// An implementation of VfsDirectory that maintains two vectors for subdirectories and files.
// Vector data is supplied upon construction.
class VectorVfsDirectory : public VfsDirectory {
public:
    explicit VectorVfsDirectory(std::vector<VirtualFile> files = {},
                                std::vector<VirtualDir> dirs = {}, std::string name = "",
                                VirtualDir parent = nullptr);

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
