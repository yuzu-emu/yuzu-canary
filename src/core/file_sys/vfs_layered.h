// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include "core/file_sys/vfs.h"

namespace FileSys {

// Wrapper function to allow for more efficient handling of dirs.size() == 0, 1 cases.
VirtualDir LayerDirectories(std::vector<VirtualDir> dirs, std::string name = "");

// Class that stacks multiple VfsDirectories on top of each other, attempting to read from the first
// one and falling back to the one after. The highest priority directory (overwrites all others)
// should be element 0 in the dirs vector.
class LayeredVfsDirectory : public VfsDirectory {
    friend VirtualDir LayerDirectories(std::vector<VirtualDir> dirs, std::string name);

    LayeredVfsDirectory(std::vector<VirtualDir> dirs, std::string name);

public:
    ~LayeredVfsDirectory() override;

    std::shared_ptr<VfsFile> GetFileRelative(std::string_view path) const override;
    std::shared_ptr<VfsDirectory> GetDirectoryRelative(std::string_view path) const override;
    std::shared_ptr<VfsFile> GetFile(std::string_view name) const override;
    std::shared_ptr<VfsDirectory> GetSubdirectory(std::string_view name) const override;
    std::string GetFullPath() const override;

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

protected:
    bool ReplaceFileWithSubdirectory(VirtualFile file, VirtualDir dir) override;

private:
    std::vector<VirtualDir> dirs;
    std::string name;
};

} // namespace FileSys
