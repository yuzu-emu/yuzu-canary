// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include "common/assert.h"
#include "core/crypto/ctr_encryption_layer.h"

namespace Core::Crypto {

CTREncryptionLayer::CTREncryptionLayer(FileSys::VirtualFile base_, Key128 key_, size_t base_offset)
    : EncryptionLayer(std::move(base_)), base_offset(base_offset), cipher(key_, Mode::CTR),
      iv(16, 0) {}

size_t CTREncryptionLayer::Read(u8* data, size_t length, size_t offset) const {
    if (length == 0)
        return 0;

    const auto sector_offset = offset & 0xF;
    if (sector_offset == 0) {
        UpdateIV(base_offset + offset);
        std::vector<u8> raw = base->ReadBytes(length, offset);
        cipher.Transcode(raw.data(), raw.size(), data, Op::Decrypt);
        return raw.size();
    }

    // offset does not fall on block boundary (0x10)
    std::vector<u8> block = base->ReadBytes(0x10, offset - sector_offset);
    UpdateIV(base_offset + offset - sector_offset);
    cipher.Transcode(block.data(), block.size(), block.data(), Op::Decrypt);
    size_t read = 0x10 - sector_offset;

    if (length + sector_offset < 0x10) {
        std::memcpy(data, block.data() + sector_offset, std::min<u64>(length, read));
        return std::min<u64>(length, read);
    }
    std::memcpy(data, block.data() + sector_offset, read);
    return read + Read(data + read, length - read, offset + read);
}

void CTREncryptionLayer::SetIV(const std::vector<u8>& iv_) {
    const auto length = std::min(iv_.size(), iv.size());
    iv.assign(iv_.cbegin(), iv_.cbegin() + length);
}

void CTREncryptionLayer::UpdateIV(size_t offset) const {
    offset >>= 4;
    for (size_t i = 0; i < 8; ++i) {
        iv[16 - i - 1] = offset & 0xFF;
        offset >>= 8;
    }
    cipher.SetIV(iv);
}
} // namespace Core::Crypto
