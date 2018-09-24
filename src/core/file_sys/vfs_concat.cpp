// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <utility>

#include "common/assert.h"
#include "core/file_sys/vfs_concat.h"

namespace FileSys {

static bool VerifyConcatenationMapContinuity(const std::map<u64, VirtualFile>& map) {
    const auto last_valid = --map.end();
    for (auto iter = map.begin(); iter != last_valid;) {
        const auto old = iter++;
        if (old->first + old->second->GetSize() != iter->first) {
            return false;
        }
    }

    return map.begin()->first == 0;
}

VirtualFile ConcatenateFiles(std::vector<VirtualFile> files, std::string name) {
    if (files.empty())
        return nullptr;
    if (files.size() == 1)
        return files[0];

    return std::shared_ptr<VfsFile>(new ConcatenatedVfsFile(std::move(files), std::move(name)));
}

ConcatenatedVfsFile::ConcatenatedVfsFile(std::vector<VirtualFile> files_, std::string name)
    : name(std::move(name)) {
    std::size_t next_offset = 0;
    for (const auto& file : files_) {
        files[next_offset] = file;
        next_offset += file->GetSize();
    }
}

ConcatenatedVfsFile::ConcatenatedVfsFile(std::map<u64, VirtualFile> files_, std::string name)
    : files(std::move(files_)), name(std::move(name)) {
    ASSERT(VerifyConcatenationMapContinuity(files));
}

ConcatenatedVfsFile::~ConcatenatedVfsFile() = default;

std::string ConcatenatedVfsFile::GetName() const {
    if (files.empty())
        return "";
    if (!name.empty())
        return name;
    return files.begin()->second->GetName();
}

std::size_t ConcatenatedVfsFile::GetSize() const {
    if (files.empty())
        return 0;
    return files.rbegin()->first + files.rbegin()->second->GetSize();
}

bool ConcatenatedVfsFile::Resize(std::size_t new_size) {
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

std::size_t ConcatenatedVfsFile::Read(u8* data, std::size_t length, std::size_t offset) const {
    auto entry = --files.end();
    for (auto iter = files.begin(); iter != files.end(); ++iter) {
        if (iter->first > offset) {
            entry = --iter;
            break;
        }
    }

    if (entry->first + entry->second->GetSize() <= offset)
        return 0;

    const auto read_in =
        std::min<u64>(entry->first + entry->second->GetSize() - offset, entry->second->GetSize());
    if (length > read_in) {
        return entry->second->Read(data, read_in, offset - entry->first) +
               Read(data + read_in, length - read_in, offset + read_in);
    }

    return entry->second->Read(data, std::min<u64>(read_in, length), offset - entry->first);
}

std::size_t ConcatenatedVfsFile::Write(const u8* data, std::size_t length, std::size_t offset) {
    return 0;
}

bool ConcatenatedVfsFile::Rename(std::string_view name) {
    return false;
}

} // namespace FileSys
