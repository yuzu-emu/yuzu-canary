// Copyright 2013 Dolphin Emulator Project / 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/hex_util.h"

u8 ToHexNibble(char c1) {
    if (c1 >= 65 && c1 <= 70)
        return c1 - 55;
    if (c1 >= 97 && c1 <= 102)
        return c1 - 87;
    if (c1 >= 48 && c1 <= 57)
        return c1 - 48;
    throw std::logic_error("Invalid hex digit");
}

std::array<u8, 16> operator""_array16(const char* str, size_t len) {
    if (len != 32)
        throw std::logic_error("Not of correct size.");
    return HexStringToArray<16>(str);
}

std::array<u8, 32> operator""_array32(const char* str, size_t len) {
    if (len != 64)
        throw std::logic_error("Not of correct size.");
    return HexStringToArray<32>(str);
}
