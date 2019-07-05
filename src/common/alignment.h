// This file is under the public domain.

#pragma once

#include <cstddef>
#include <type_traits>

namespace Common {

template <typename T>
constexpr T AlignUp(T value, std::size_t size) {
    static_assert(std::is_unsigned_v<T>, "T must be an unsigned value.");
    return static_cast<T>(value + (size - value % size) % size);
}

template <typename T>
constexpr T AlignDown(T value, std::size_t size) {
    static_assert(std::is_unsigned_v<T>, "T must be an unsigned value.");
    return static_cast<T>(value - value % size);
}

template <typename T>
constexpr T AlignBits(T value, std::size_t align) {
    static_assert(std::is_unsigned_v<T>, "T must be an unsigned value.");
    return static_cast<T>((value + ((1ULL << align) - 1)) >> align << align);
}

template <typename T>
constexpr bool Is4KBAligned(T value) {
    static_assert(std::is_unsigned_v<T>, "T must be an unsigned value.");
    return (value & 0xFFF) == 0;
}

template <typename T>
constexpr bool IsWordAligned(T value) {
    static_assert(std::is_unsigned_v<T>, "T must be an unsigned value.");
    return (value & 0b11) == 0;
}

} // namespace Common
