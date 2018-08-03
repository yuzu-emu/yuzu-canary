// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include "boost/optional.hpp"
#include "common/common_types.h"

namespace FileSys {
struct VfsFile;
struct VfsDirectory;

// Convenience typedefs to use VfsDirectory and VfsFile
using VirtualDir = std::shared_ptr<FileSys::VfsDirectory>;
using VirtualFile = std::shared_ptr<FileSys::VfsFile>;

// A class representing a file in an abstract filesystem.
struct VfsFile : NonCopyable {
    virtual ~VfsFile();

    // Retrieves the file name.
    virtual std::string GetName() const = 0;
    // Retrieves the extension of the file name.
    virtual std::string GetExtension() const;
    // Retrieves the size of the file.
    virtual size_t GetSize() const = 0;
    // Resizes the file to new_size. Returns whether or not the operation was successful.
    virtual bool Resize(size_t new_size) = 0;
    // Gets a pointer to the directory containing this file, returning nullptr if there is none.
    virtual std::shared_ptr<VfsDirectory> GetContainingDirectory() const = 0;

    // Returns whether or not the file can be written to.
    virtual bool IsWritable() const = 0;
    // Returns whether or not the file can be read from.
    virtual bool IsReadable() const = 0;

    // The primary method of reading from the file. Reads length bytes into data starting at offset
    // into file. Returns number of bytes successfully read.
    virtual size_t Read(u8* data, size_t length, size_t offset = 0) const = 0;
    // The primary method of writing to the file. Writes length bytes from data starting at offset
    // into file. Returns number of bytes successfully written.
    virtual size_t Write(const u8* data, size_t length, size_t offset = 0) = 0;

    // Reads exactly one byte at the offset provided, returning boost::none on error.
    virtual boost::optional<u8> ReadByte(size_t offset = 0) const;
    // Reads size bytes starting at offset in file into a vector.
    virtual std::vector<u8> ReadBytes(size_t size, size_t offset = 0) const;
    // Reads all the bytes from the file into a vector. Equivalent to 'file->Read(file->GetSize(),
    // 0)'
    virtual std::vector<u8> ReadAllBytes() const;

    // Reads an array of type T, size number_elements starting at offset.
    // Returns the number of bytes (sizeof(T)*number_elements) read successfully.
    template <typename T>
    size_t ReadArray(T* data, size_t number_elements, size_t offset = 0) const {
        static_assert(std::is_trivially_copyable_v<T>, "Data type must be trivially copyable.");

        return Read(reinterpret_cast<u8*>(data), number_elements * sizeof(T), offset);
    }

    // Reads size bytes into the memory starting at data starting at offset into the file.
    // Returns the number of bytes read successfully.
    template <typename T>
    size_t ReadBytes(T* data, size_t size, size_t offset = 0) const {
        static_assert(std::is_trivially_copyable_v<T>, "Data type must be trivially copyable.");
        return Read(reinterpret_cast<u8*>(data), size, offset);
    }

    // Reads one object of type T starting at offset in file.
    // Returns the number of bytes read successfully (sizeof(T)).
    template <typename T>
    size_t ReadObject(T* data, size_t offset = 0) const {
        static_assert(std::is_trivially_copyable_v<T>, "Data type must be trivially copyable.");
        return Read(reinterpret_cast<u8*>(data), sizeof(T), offset);
    }

    // Writes exactly one byte to offset in file and retuns whether or not the byte was written
    // successfully.
    virtual bool WriteByte(u8 data, size_t offset = 0);
    // Writes a vector of bytes to offset in file and returns the number of bytes successfully
    // written.
    virtual size_t WriteBytes(const std::vector<u8>& data, size_t offset = 0);

    // Writes an array of type T, size number_elements to offset in file.
    // Returns the number of bytes (sizeof(T)*number_elements) written successfully.
    template <typename T>
    size_t WriteArray(const T* data, size_t number_elements, size_t offset = 0) {
        static_assert(std::is_trivially_copyable_v<T>, "Data type must be trivially copyable.");
        return Write(data, number_elements * sizeof(T), offset);
    }

    // Writes size bytes starting at memory location data to offset in file.
    // Returns the number of bytes written successfully.
    template <typename T>
    size_t WriteBytes(const T* data, size_t size, size_t offset = 0) {
        static_assert(std::is_trivially_copyable_v<T>, "Data type must be trivially copyable.");
        return Write(reinterpret_cast<const u8*>(data), size, offset);
    }

    // Writes one object of type T to offset in file.
    // Returns the number of bytes written successfully (sizeof(T)).
    template <typename T>
    size_t WriteObject(const T& data, size_t offset = 0) {
        static_assert(std::is_trivially_copyable_v<T>, "Data type must be trivially copyable.");
        return Write(&data, sizeof(T), offset);
    }

    // Renames the file to name. Returns whether or not the operation was successsful.
    virtual bool Rename(std::string_view name) = 0;

    // Returns the full path of this file as a string, recursively
    virtual std::string GetFullPath() const;
};

// A class representing a directory in an abstract filesystem.
struct VfsDirectory : NonCopyable {
    virtual ~VfsDirectory();

    // Retrives the file located at path as if the current directory was root. Returns nullptr if
    // not found.
    virtual std::shared_ptr<VfsFile> GetFileRelative(std::string_view path) const;
    // Calls GetFileRelative(path) on the root of the current directory.
    virtual std::shared_ptr<VfsFile> GetFileAbsolute(std::string_view path) const;

    // Retrives the directory located at path as if the current directory was root. Returns nullptr
    // if not found.
    virtual std::shared_ptr<VfsDirectory> GetDirectoryRelative(std::string_view path) const;
    // Calls GetDirectoryRelative(path) on the root of the current directory.
    virtual std::shared_ptr<VfsDirectory> GetDirectoryAbsolute(std::string_view path) const;

    // Returns a vector containing all of the files in this directory.
    virtual std::vector<std::shared_ptr<VfsFile>> GetFiles() const = 0;
    // Returns the file with filename matching name. Returns nullptr if directory dosen't have a
    // file with name.
    virtual std::shared_ptr<VfsFile> GetFile(std::string_view name) const;

    // Returns a vector containing all of the subdirectories in this directory.
    virtual std::vector<std::shared_ptr<VfsDirectory>> GetSubdirectories() const = 0;
    // Returns the directory with name matching name. Returns nullptr if directory dosen't have a
    // directory with name.
    virtual std::shared_ptr<VfsDirectory> GetSubdirectory(std::string_view name) const;

    // Returns whether or not the directory can be written to.
    virtual bool IsWritable() const = 0;
    // Returns whether of not the directory can be read from.
    virtual bool IsReadable() const = 0;

    // Returns whether or not the directory is the root of the current file tree.
    virtual bool IsRoot() const;

    // Returns the name of the directory.
    virtual std::string GetName() const = 0;
    // Returns the total size of all files and subdirectories in this directory.
    virtual size_t GetSize() const;
    // Returns the parent directory of this directory. Returns nullptr if this directory is root or
    // has no parent.
    virtual std::shared_ptr<VfsDirectory> GetParentDirectory() const = 0;

    // Creates a new subdirectory with name name. Returns a pointer to the new directory or nullptr
    // if the operation failed.
    virtual std::shared_ptr<VfsDirectory> CreateSubdirectory(std::string_view name) = 0;
    // Creates a new file with name name. Returns a pointer to the new file or nullptr if the
    // operation failed.
    virtual std::shared_ptr<VfsFile> CreateFile(std::string_view name) = 0;

    // Creates a new file at the path relative to this directory. Also creates directories if
    // they do not exist and is supported by this implementation. Returns nullptr on any failure.
    virtual std::shared_ptr<VfsFile> CreateFileRelative(std::string_view path);

    // Creates a new file at the path relative to root of this directory. Also creates directories
    // if they do not exist and is supported by this implementation. Returns nullptr on any failure.
    virtual std::shared_ptr<VfsFile> CreateFileAbsolute(std::string_view path);

    // Creates a new directory at the path relative to this directory. Also creates directories if
    // they do not exist and is supported by this implementation. Returns nullptr on any failure.
    virtual std::shared_ptr<VfsDirectory> CreateDirectoryRelative(std::string_view path);

    // Creates a new directory at the path relative to root of this directory. Also creates
    // directories if they do not exist and is supported by this implementation. Returns nullptr on
    // any failure.
    virtual std::shared_ptr<VfsDirectory> CreateDirectoryAbsolute(std::string_view path);

    // Deletes the subdirectory with name and returns true on success.
    virtual bool DeleteSubdirectory(std::string_view name) = 0;
    // Deletes all subdirectories and files of subdirectory with name recirsively and then deletes
    // the subdirectory. Returns true on success.
    virtual bool DeleteSubdirectoryRecursive(std::string_view name);
    // Returnes whether or not the file with name name was deleted successfully.
    virtual bool DeleteFile(std::string_view name) = 0;

    // Returns whether or not this directory was renamed to name.
    virtual bool Rename(std::string_view name) = 0;

    // Returns whether or not the file with name src was successfully copied to a new file with name
    // dest.
    virtual bool Copy(std::string_view src, std::string_view dest);

    // Interprets the file with name file instead as a directory of type directory.
    // The directory must have a constructor that takes a single argument of type
    // std::shared_ptr<VfsFile>. Allows to reinterpret container files (i.e NCA, zip, XCI, etc) as a
    // subdirectory in one call.
    template <typename Directory>
    bool InterpretAsDirectory(std::string_view file) {
        auto file_p = GetFile(file);

        if (file_p == nullptr) {
            return false;
        }

        return ReplaceFileWithSubdirectory(file_p, std::make_shared<Directory>(file_p));
    }

    bool InterpretAsDirectory(const std::function<VirtualDir(VirtualFile)>& function,
                              const std::string& file) {
        auto file_p = GetFile(file);
        if (file_p == nullptr)
            return false;
        return ReplaceFileWithSubdirectory(file_p, function(file_p));
    }

    // Returns the full path of this directory as a string, recursively
    virtual std::string GetFullPath() const;

protected:
    // Backend for InterpretAsDirectory.
    // Removes all references to file and adds a reference to dir in the directory's implementation.
    virtual bool ReplaceFileWithSubdirectory(VirtualFile file, VirtualDir dir) = 0;
};

// A convenience partial-implementation of VfsDirectory that stubs out methods that should only work
// if writable. This is to avoid redundant empty methods everywhere.
struct ReadOnlyVfsDirectory : public VfsDirectory {
    bool IsWritable() const override;
    bool IsReadable() const override;
    std::shared_ptr<VfsDirectory> CreateSubdirectory(std::string_view name) override;
    std::shared_ptr<VfsFile> CreateFile(std::string_view name) override;
    bool DeleteSubdirectory(std::string_view name) override;
    bool DeleteFile(std::string_view name) override;
    bool Rename(std::string_view name) override;
};

// Compare the two files, byte-for-byte, in increments specificed by block_size
bool DeepEquals(const VirtualFile& file1, const VirtualFile& file2, size_t block_size = 0x200);

// A method that copies the raw data between two different implementations of VirtualFile. If you
// are using the same implementation, it is probably better to use the Copy method in the parent
// directory of src/dest.
bool VfsRawCopy(VirtualFile src, VirtualFile dest);

} // namespace FileSys
