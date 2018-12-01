// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstring>
#include "core/file_sys/vfs.h"

namespace FileSys {

// An implementation of VfsFile that is backed by a statically-sized array
template <std::size_t size>
class ArrayVfsFile : public VfsFile {
public:
    ArrayVfsFile(std::array<u8, size> data, std::string name = "", VirtualDir parent = nullptr)
        : data(data), name(std::move(name)), parent(std::move(parent)) {}

    std::string GetName() const override {
        return name;
    }

    std::size_t GetSize() const override {
        return size;
    }

    bool Resize(std::size_t new_size) override {
        return false;
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

    std::size_t Read(u8* data_, std::size_t length, std::size_t offset) const override {
        const auto read = std::min(length, size - offset);
        std::memcpy(data_, data.data() + offset, read);
        return read;
    }

    std::size_t Write(const u8* data, std::size_t length, std::size_t offset) override {
        return 0;
    }

    bool Rename(std::string_view name) override {
        this->name = name;
        return true;
    }

private:
    std::array<u8, size> data;
    std::string name;
    VirtualDir parent;
};

// An implementation of VfsFile that is backed by a vector optionally supplied upon construction
class VectorVfsFile : public VfsFile {
public:
    explicit VectorVfsFile(std::vector<u8> initial_data = {}, std::string name = "",
                           VirtualDir parent = nullptr);
    ~VectorVfsFile() override;

    std::string GetName() const override;
    std::size_t GetSize() const override;
    bool Resize(std::size_t new_size) override;
    std::shared_ptr<VfsDirectory> GetContainingDirectory() const override;
    bool IsWritable() const override;
    bool IsReadable() const override;
    std::size_t Read(u8* data, std::size_t length, std::size_t offset) const override;
    std::size_t Write(const u8* data, std::size_t length, std::size_t offset) override;
    bool Rename(std::string_view name) override;

    virtual void Assign(std::vector<u8> new_data);

private:
    std::vector<u8> data;
    VirtualDir parent;
    std::string name;
};

// An implementation of VfsDirectory that maintains two vectors for subdirectories and files.
// Vector data is supplied upon construction.
class VectorVfsDirectory : public VfsDirectory {
public:
    explicit VectorVfsDirectory(std::vector<VirtualFile> files = {},
                                std::vector<VirtualDir> dirs = {}, std::string name = "",
                                VirtualDir parent = nullptr);
    ~VectorVfsDirectory() override;

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

private:
    std::vector<VirtualFile> files;
    std::vector<VirtualDir> dirs;

    VirtualDir parent;
    std::string name;
};

} // namespace FileSys
