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

inline u32 CountLeadingZeroes64(u64 value) {
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

    return static_cast<u32>(__builtin_clz(value));
}

inline u32 CountLeadingZeroes64(u64 value) {
    if (value == 0) {
        return 64;
    }

    return static_cast<u32>(__builtin_clzll(value));
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

inline u32 CountTrailingZeroes64(u64 value) {
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

    return static_cast<u32>(__builtin_ctz(value));
}

inline u32 CountTrailingZeroes64(u64 value) {
    if (value == 0) {
        return 64;
    }

    return static_cast<u32>(__builtin_ctzll(value));
}
#endif

#ifdef _MSC_VER

inline u32 MostSignificantBit32(const u32 value) {
    unsigned long result;
    _BitScanReverse(&result, value);
    return static_cast<u32>(result);
}

inline u32 MostSignificantBit64(const u64 value) {
    unsigned long result;
    _BitScanReverse64(&result, value);
    return static_cast<u32>(result);
}

#else

inline u32 MostSignificantBit32(const u32 value) {
    return 31U - static_cast<u32>(__builtin_clz(value));
}

inline u32 MostSignificantBit64(const u64 value) {
    return 63U - static_cast<u32>(__builtin_clzll(value));
}

#endif

inline u32 Log2Floor32(const u32 value) {
    return MostSignificantBit32(value);
}

inline u32 Log2Ceil32(const u32 value) {
    const u32 log2_f = Log2Floor32(value);
    return log2_f + ((value ^ (1U << log2_f)) != 0U);
}

inline u32 Log2Floor64(const u64 value) {
    return MostSignificantBit64(value);
}

inline u32 Log2Ceil64(const u64 value) {
    const u64 log2_f = static_cast<u64>(Log2Floor64(value));
    return static_cast<u32>(log2_f + ((value ^ (1ULL << log2_f)) != 0ULL));
}

} // namespace Common
