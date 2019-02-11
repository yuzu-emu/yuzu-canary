// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"

#include <vector>

namespace Common::VirtualFileUtil {

class VirtualBinaryFile {
public:
    VirtualBinaryFile();
    VirtualBinaryFile(std::size_t preallocated_size);
    ~VirtualBinaryFile();

    template <typename T>
    std::size_t ReadVector(std::vector<T>& vec) {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        return ReadBytes(vec.data(), vec.size());
    }

    template <typename T>
    std::size_t WriteVector(const std::vector<T>& vec) {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        return WriteBytes(vec.data(), vec.size());
    }

    template <typename T>
    std::size_t ReadArray(T* data, std::size_t size) {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        return ReadBytes(data, size);
    }

    template <typename T>
    std::size_t WriteArray(const T* data, std::size_t size) {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        return WriteBytes(data, size);
    }

    template <typename T>
    std::size_t ReadObject(T& object) {
        static_assert(!std::is_pointer_v<T>, "ReadObject arguments must not be a pointer");
        return ReadArray(&object, 1);
    }

    template <typename T>
    std::size_t WriteObject(const T& object) {
        static_assert(!std::is_pointer_v<T>, "WriteObject arguments must not be a pointer");
        return WriteArray(&object, 1);
    }

    /// @return the current position of the virtual file on which the next read or write request
    /// will start
    std::size_t GetCurrentReadWritePosition() const;

    /// set the current position of the virtual file on which the next read or write request
    /// will start to the start of the virtual file (position = 0)
    void ResetReadWritePosition();

    /// @return pointer to the first byte of the virtual file
    u8* const Data();

    /// @return size in bytes of the virtual file
    std::size_t Size() const;

    /// Resizes m_file_storage to size without wiping the remaining original data in m_file_storage.
    /// If m_file_storage is smaller than the target size, the remaining m_file_storage appends and
    /// will be filled with zeros. m_file_storage_current_position does not change in this case. If
    /// m_file_storage is larger than the target size, the rest of the m_file_storage gets deleted
    /// and m_file_storage_current_position will point to the last byte of m_file_storage.
    const void Resize(std::size_t size);

private:
    template <typename T>
    std::size_t ReadBytes(T* data, std::size_t length) {
        const std::size_t length_in_bytes = sizeof(T) * length;

        if (RequestExceedsFileSize(length_in_bytes)) {
            return std::numeric_limits<std::size_t>::max();
        }

        const u8* const start_position = m_file_storage.data() + m_file_storage_current_position;
        const u8* const end_position =
            m_file_storage.data() + m_file_storage_current_position + length_in_bytes;

        std::copy(start_position, end_position, reinterpret_cast<u8*>(data));
        m_file_storage_current_position += length_in_bytes;
        return length;
    }

    template <typename T>
    std::size_t WriteBytes(const T* data, std::size_t length) {
        const std::size_t length_in_bytes = sizeof(T) * length;

        m_file_storage.insert(m_file_storage.begin() + m_file_storage_current_position,
                              reinterpret_cast<const u8*>(data),
                              reinterpret_cast<const u8*>(data) + length_in_bytes);
        m_file_storage_current_position += length_in_bytes;
        return length;
    }

    bool RequestExceedsFileSize(std::size_t length_in_bytes);

    std::vector<u8> m_file_storage;
    std::size_t m_file_storage_current_position;
};

} // namespace Common::VirtualFileUtil