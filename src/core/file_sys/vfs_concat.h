// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <string_view>
#include <boost/container/flat_map.hpp>
#include "core/file_sys/vfs.h"

namespace FileSys {

// Wrapper function to allow for more efficient handling of files.size() == 0, 1 cases.
VirtualFile ConcatenateFiles(std::vector<VirtualFile> files, std::string name = "");

// Class that wraps multiple vfs files and concatenates them, making reads seamless. Currently
// read-only.
class ConcatenatedVfsFile : public VfsFile {
    friend VirtualFile ConcatenateFiles(std::vector<VirtualFile> files, std::string name);

    ConcatenatedVfsFile(std::vector<VirtualFile> files, std::string name);

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
    // Maps starting offset to file -- more efficient.
    boost::container::flat_map<u64, VirtualFile> files;
    std::string name;
};

} // namespace FileSys
