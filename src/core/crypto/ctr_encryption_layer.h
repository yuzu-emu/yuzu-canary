// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/crypto/aes_util.h"
#include "core/crypto/encryption_layer.h"
#include "core/crypto/key_manager.h"

namespace Core::Crypto {

// Sits on top of a VirtualFile and provides CTR-mode AES decription.
class CTREncryptionLayer : public EncryptionLayer {
public:
    CTREncryptionLayer(FileSys::VirtualFile base, Key128 key, size_t base_offset);

    size_t Read(u8* data, size_t length, size_t offset) const override;

    void SetIV(const std::vector<u8>& iv);

private:
    size_t base_offset;

    // Must be mutable as operations modify cipher contexts.
    mutable AESCipher<Key128> cipher;
    mutable std::vector<u8> iv;

    void UpdateIV(size_t offset) const;
};

} // namespace Core::Crypto
