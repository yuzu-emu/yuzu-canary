// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/assert.h"
#include "core/file_sys/vfs.h"

namespace Core::Crypto {

enum class Mode {
    CTR = 11,
    ECB = 2,
    XTS = 70,
};

enum class Op {
    Encrypt,
    Decrypt,
};

struct CipherContext;

template <typename Key, size_t KeySize = sizeof(Key)>
class AESCipher {
    static_assert(std::is_same_v<Key, std::array<u8, KeySize>>, "Key must be std::array of u8.");
    static_assert(KeySize == 0x10 || KeySize == 0x20, "KeySize must be 128 or 256.");

public:
    AESCipher(Key key, Mode mode);

    ~AESCipher();

    void SetIV(std::vector<u8> iv);

    template <typename Source, typename Dest>
    void Transcode(const Source* src, size_t size, Dest* dest, Op op) {
        Transcode(reinterpret_cast<const u8*>(src), size, reinterpret_cast<u8*>(dest), op);
    }

    void Transcode(const u8* src, size_t size, u8* dest, Op op);

    template <typename Source, typename Dest>
    void XTSTranscode(const Source* src, size_t size, Dest* dest, size_t sector_id,
                      size_t sector_size, Op op) {
        XTSTranscode(reinterpret_cast<const u8*>(src), size, reinterpret_cast<u8*>(dest), sector_id,
                     sector_size, op);
    }

    void XTSTranscode(const u8* src, size_t size, u8* dest, size_t sector_id, size_t sector_size,
                      Op op);

private:
    std::unique_ptr<CipherContext> ctx;

    static std::vector<u8> CalculateNintendoTweak(size_t sector_id);
};
} // namespace Core::Crypto
