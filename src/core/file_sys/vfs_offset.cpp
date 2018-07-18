// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/file_sys/vfs_offset.h"

namespace FileSys {

OffsetVfsFile::OffsetVfsFile(std::shared_ptr<VfsFile> file_, size_t size_, size_t offset_,
                             const std::string& name_)
    : file(file_), offset(offset_), size(size_), name(name_) {}

std::string OffsetVfsFile::GetName() const {
    return name.empty() ? file->GetName() : name;
}

size_t OffsetVfsFile::GetSize() const {
    return size;
}

bool OffsetVfsFile::Resize(size_t new_size) {
    if (offset + new_size < file->GetSize()) {
        size = new_size;
    } else {
        auto res = file->Resize(offset + new_size);
        if (!res)
            return false;
        size = new_size;
    }

    return true;
}

std::shared_ptr<VfsDirectory> OffsetVfsFile::GetContainingDirectory() const {
    return file->GetContainingDirectory();
}

bool OffsetVfsFile::IsWritable() const {
    return file->IsWritable();
}

bool OffsetVfsFile::IsReadable() const {
    return file->IsReadable();
}

size_t OffsetVfsFile::Read(u8* data, size_t length, size_t r_offset) const {
    return file->Read(data, TrimToFit(length, r_offset), offset + r_offset);
}

size_t OffsetVfsFile::Write(const u8* data, size_t length, size_t r_offset) {
    return file->Write(data, TrimToFit(length, r_offset), offset + r_offset);
}

boost::optional<u8> OffsetVfsFile::ReadByte(size_t r_offset) const {
    if (r_offset < size)
        return file->ReadByte(offset + r_offset);

    return boost::none;
}

std::vector<u8> OffsetVfsFile::ReadBytes(size_t r_size, size_t r_offset) const {
    return file->ReadBytes(TrimToFit(r_size, r_offset), offset + r_offset);
}

std::vector<u8> OffsetVfsFile::ReadAllBytes() const {
    return file->ReadBytes(size, offset);
}

bool OffsetVfsFile::WriteByte(u8 data, size_t r_offset) {
    if (r_offset < size)
        return file->WriteByte(data, offset + r_offset);

    return false;
}

size_t OffsetVfsFile::WriteBytes(std::vector<u8> data, size_t r_offset) {
    return file->Write(data.data(), TrimToFit(data.size(), r_offset), offset + r_offset);
}

bool OffsetVfsFile::Rename(const std::string& name) {
    return file->Rename(name);
}

size_t OffsetVfsFile::GetOffset() const {
    return offset;
}

size_t OffsetVfsFile::TrimToFit(size_t r_size, size_t r_offset) const {
    return std::max<size_t>(std::min<size_t>(size - r_offset, r_size), 0);
}

} // namespace FileSys
