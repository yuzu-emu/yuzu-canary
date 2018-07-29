// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/file_sys/vfs.h"

namespace Core::Crypto {

// Basically non-functional class that implements all of the methods that are irrelevant to an
// EncryptionLayer. Reduces duplicate code.
struct EncryptionLayer : public FileSys::VfsFile {
    explicit EncryptionLayer(FileSys::VirtualFile base);

    size_t Read(u8* data, size_t length, size_t offset) const override = 0;

    std::string GetName() const override;
    size_t GetSize() const override;
    bool Resize(size_t new_size) override;
    std::shared_ptr<FileSys::VfsDirectory> GetContainingDirectory() const override;
    bool IsWritable() const override;
    bool IsReadable() const override;
    size_t Write(const u8* data, size_t length, size_t offset) override;
    bool Rename(std::string_view name) override;

protected:
    FileSys::VirtualFile base;
};

} // namespace Core::Crypto
