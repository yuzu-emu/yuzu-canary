// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>
#include <memory>
#include <string_view>

#include "core/file_sys/vfs.h"

namespace FileSys {

template <u8 value>
class StaticVfsFile : public VfsFile {
public:
    explicit StaticVfsFile(size_t size = 0, std::string name = "", VirtualDir parent = nullptr)
        : size(size), name(std::move(name)), parent(std::move(parent)) {}

    std::string GetName() const override {
        return name;
    }

    size_t GetSize() const override {
        return size;
    }

    bool Resize(size_t new_size) override {
        size = new_size;
        return true;
    }

    std::shared_ptr<VfsDirectory> GetContainingDirectory() const override {
        return parent;
    }

    bool IsWritable() const override {
        return false;
    }

    bool IsReadable() const override {
        return true;
    }

    size_t Read(u8* data, size_t length, size_t offset) const override {
        const auto read = std::min(length, size - offset);
        std::fill(data, data + read, value);
        return read;
    }

    size_t Write(const u8* data, size_t length, size_t offset) override {
        return 0;
    }

    boost::optional<u8> ReadByte(size_t offset) const override {
        if (offset < size)
            return value;
        return boost::none;
    }

    std::vector<u8> ReadBytes(size_t length, size_t offset) const override {
        const auto read = std::min(length, size - offset);
        return std::vector<u8>(read, value);
    }

    bool Rename(std::string_view new_name) override {
        name = new_name;
        return true;
    }

private:
    size_t size;
    std::string name;
    VirtualDir parent;
};

} // namespace FileSys
