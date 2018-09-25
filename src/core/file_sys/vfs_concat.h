// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <map>
#include <memory>
#include <string_view>
#include <boost/container/flat_map.hpp>
#include "core/file_sys/vfs.h"
#include "core/file_sys/vfs_static.h"

namespace FileSys {

// Class that wraps multiple vfs files and concatenates them, making reads seamless. Currently
// read-only.
class ConcatenatedVfsFile : public VfsFile {
    friend VirtualFile ConcatenateFiles(std::vector<VirtualFile> files, std::string name);

    template <u8 filler_byte>
    friend VirtualFile ConcatenateFiles(std::map<u64, VirtualFile> files, std::string name);

    ConcatenatedVfsFile(std::vector<VirtualFile> files, std::string name);
    ConcatenatedVfsFile(std::map<u64, VirtualFile> files, std::string name);

public:
    ~ConcatenatedVfsFile() override;

    std::string GetName() const override;
    std::size_t GetSize() const override;
    bool Resize(std::size_t new_size) override;
    std::shared_ptr<VfsDirectory> GetContainingDirectory() const override;
    bool IsWritable() const override;
    bool IsReadable() const override;
    std::size_t Read(u8* data, std::size_t length, std::size_t offset) const override;
    std::size_t Write(const u8* data, std::size_t length, std::size_t offset) override;
    bool Rename(std::string_view name) override;

private:
    // Maps starting offset to file -- more efficient.
    std::map<u64, VirtualFile> files;
    std::string name;
};

// Wrapper function to allow for more efficient handling of files.size() == 0, 1 cases.
VirtualFile ConcatenateFiles(std::vector<VirtualFile> files, std::string name);

// Convenience function that turns a map of offsets to files into a concatenated file, filling gaps
// with template parameter.
template <u8 filler_byte>
VirtualFile ConcatenateFiles(std::map<u64, VirtualFile> files, std::string name) {
    if (files.empty())
        return nullptr;
    if (files.size() == 1)
        return files.begin()->second;

    const auto last_valid = --files.end();
    for (auto iter = files.begin(); iter != last_valid;) {
        const auto old = iter++;
        if (old->first + old->second->GetSize() != iter->first) {
            files.emplace(old->first + old->second->GetSize(),
                          std::make_shared<StaticVfsFile<filler_byte>>(iter->first - old->first -
                                                                       old->second->GetSize()));
        }
    }

    // Ensure the map starts at offset 0 (start of file), otherwise pad to fill.
    if (files.begin()->first != 0)
        files.emplace(0, std::make_shared<StaticVfsFile<filler_byte>>(files.begin()->first));

    return std::shared_ptr<VfsFile>(new ConcatenatedVfsFile(std::move(files), std::move(name)));
}

} // namespace FileSys
