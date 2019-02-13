// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "virtual_file_util.h"

namespace Common::VirtualFileUtil {
VirtualBinaryFile::VirtualBinaryFile() {
    VirtualBinaryFile(0);
}

VirtualBinaryFile::VirtualBinaryFile(std::size_t preallocated_size) {
    m_file_storage = std::vector<u8>(preallocated_size);
    m_file_storage_current_position = 0;
}

VirtualBinaryFile::~VirtualBinaryFile() {}

void VirtualBinaryFile::ResetReadWritePosition() {
    m_file_storage_current_position = 0;
}

u8* const VirtualBinaryFile::Data() {
    return m_file_storage.data();
}

std::size_t VirtualBinaryFile::Size() const {
    return m_file_storage.size();
}

const void VirtualBinaryFile::Resize(std::size_t size) {
    m_file_storage.resize(size);
    if (size < m_file_storage_current_position) {
        m_file_storage_current_position = size;
    }
}

std::size_t VirtualBinaryFile::GetCurrentReadWritePosition() const {
    return m_file_storage_current_position;
}

bool VirtualBinaryFile::RequestExceedsFileSize(std::size_t length_in_bytes) {
    return m_file_storage_current_position + length_in_bytes > m_file_storage.size();
}

} // namespace Common::VirtualFileUtil
