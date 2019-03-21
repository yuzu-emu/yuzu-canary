// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <climits>
#include <cstddef>

#ifdef _MSC_VER
#include <intrin.h>
#endif

#include "common/common_types.h"

namespace Common {

/// Gets the size of a specified type T in bits.
template <typename T>
constexpr std::size_t BitSize() {
    return sizeof(T) * CHAR_BIT;
}

#ifdef _MSC_VER
inline u32 CountLeadingZeroes32(u32 value) {
    unsigned long leading_zero = 0;

    if (_BitScanReverse(&leading_zero, value) != 0) {
        return 31 - leading_zero;
    }

    return 32;
}

inline u64 CountLeadingZeroes64(u64 value) {
    unsigned long leading_zero = 0;

    if (_BitScanReverse64(&leading_zero, value) != 0) {
        return 63 - leading_zero;
    }

    return 64;
}
#else
inline u32 CountLeadingZeroes32(u32 value) {
    if (value == 0) {
        return 32;
    }

    return __builtin_clz(value);
}

inline u64 CountLeadingZeroes64(u64 value) {
    if (value == 0) {
        return 64;
    }

    return __builtin_clzll(value);
}
#endif

#ifdef _MSC_VER
inline u32 CountTrailingZeroes32(u32 value) {
    unsigned long trailing_zero = 0;

    if (_BitScanForward(&trailing_zero, value) != 0) {
        return trailing_zero;
    }

    return 32;
}

inline u64 CountTrailingZeroes64(u64 value) {
    unsigned long trailing_zero = 0;

    if (_BitScanForward64(&trailing_zero, value) != 0) {
        return trailing_zero;
    }

    return 64;
}
#else
inline u32 CountTrailingZeroes32(u32 value) {
    if (value == 0) {
        return 32;
    }

    return __builtin_ctz(value);
}

inline u64 CountTrailingZeroes64(u64 value) {
    if (value == 0) {
        return 64;
    }

    return __builtin_ctzll(value);
}
#endif

} // namespace Common
