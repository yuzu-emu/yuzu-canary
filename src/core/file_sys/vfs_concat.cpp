// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <utility>

#include "core/file_sys/vfs_concat.h"

namespace FileSys {

VirtualFile ConcatenateFiles(std::vector<VirtualFile> files, std::string name) {
    if (files.empty())
        return nullptr;
    if (files.size() == 1)
        return files[0];

    return std::shared_ptr<VfsFile>(new ConcatenatedVfsFile(std::move(files), std::move(name)));
}

ConcatenatedVfsFile::ConcatenatedVfsFile(std::vector<VirtualFile> files_, std::string name)
    : name(std::move(name)) {
    size_t next_offset = 0;
    for (const auto& file : files_) {
        files[next_offset] = file;
        next_offset += file->GetSize();
    }
}

std::string ConcatenatedVfsFile::GetName() const {
    if (files.empty())
        return "";
    if (!name.empty())
        return name;
    return files.begin()->second->GetName();
}

size_t ConcatenatedVfsFile::GetSize() const {
    if (files.empty())
        return 0;
    return files.rbegin()->first + files.rbegin()->second->GetSize();
}

bool ConcatenatedVfsFile::Resize(size_t new_size) {
    return false;
}

std::shared_ptr<VfsDirectory> ConcatenatedVfsFile::GetContainingDirectory() const {
    if (files.empty())
        return nullptr;
    return files.begin()->second->GetContainingDirectory();
}

bool ConcatenatedVfsFile::IsWritable() const {
    return false;
}

bool ConcatenatedVfsFile::IsReadable() const {
    return true;
}

size_t ConcatenatedVfsFile::Read(u8* data, size_t length, size_t offset) const {
    auto entry = files.end();
    for (auto iter = files.begin(); iter != files.end(); ++iter) {
        if (iter->first > offset) {
            entry = --iter;
            break;
        }
    }

    // Check if the entry should be the last one. The loop above will make it end().
    if (entry == files.end() && offset < files.rbegin()->first + files.rbegin()->second->GetSize())
        --entry;

    if (entry == files.end())
        return 0;

    const auto remaining = entry->second->GetSize() + offset - entry->first;
    if (length > remaining) {
        return entry->second->Read(data, remaining, offset - entry->first) +
               Read(data + remaining, length - remaining, offset + remaining);
    }

    return entry->second->Read(data, length, offset - entry->first);
}

size_t ConcatenatedVfsFile::Write(const u8* data, size_t length, size_t offset) {
    return 0;
}

bool ConcatenatedVfsFile::Rename(std::string_view name) {
    return false;
}
} // namespace FileSys
