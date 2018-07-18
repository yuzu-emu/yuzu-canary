// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/file_sys/vfs.h"

namespace FileSys {

// An implementation of VfsFile that wraps around another VfsFile at a certain offset.
// Similar to seeking to an offset.
// If the file is writable, operations that would write past the end of the offset file will expand
// the size of this wrapper.
struct OffsetVfsFile : public VfsFile {
    OffsetVfsFile(std::shared_ptr<VfsFile> file, size_t size, size_t offset = 0,
                  const std::string& new_name = "");

    std::string GetName() const override;
    size_t GetSize() const override;
    bool Resize(size_t new_size) override;
    std::shared_ptr<VfsDirectory> GetContainingDirectory() const override;
    bool IsWritable() const override;
    bool IsReadable() const override;
    size_t Read(u8* data, size_t length, size_t offset) const override;
    size_t Write(const u8* data, size_t length, size_t offset) override;
    boost::optional<u8> ReadByte(size_t offset) const override;
    std::vector<u8> ReadBytes(size_t size, size_t offset) const override;
    std::vector<u8> ReadAllBytes() const override;
    bool WriteByte(u8 data, size_t offset) override;
    size_t WriteBytes(std::vector<u8> data, size_t offset) override;

    bool Rename(const std::string& name) override;

    size_t GetOffset() const;

private:
    size_t TrimToFit(size_t r_size, size_t r_offset) const;

    std::shared_ptr<VfsFile> file;
    size_t offset;
    size_t size;
    std::string name;
};

} // namespace FileSys
