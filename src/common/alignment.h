// This file is under the public domain.

#pragma once

#include <cstddef>
#include <memory>
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

template <typename T, std::size_t Align = 16>
class AlignmentAllocator {
public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    using pointer = T*;
    using const_pointer = const T*;

    using reference = T&;
    using const_reference = const T&;

public:
    pointer address(reference r) noexcept {
        return std::addressof(r);
    }

    const_pointer address(const_reference r) const noexcept {
        return std::addressof(r);
    }

    pointer allocate(size_type n) {
        return static_cast<pointer>(::operator new (n, std::align_val_t{Align}));
    }

    void deallocate(pointer p, size_type) {
        ::operator delete (p, std::align_val_t{Align});
    }

    void construct(pointer p, const value_type& wert) {
        new (p) value_type(wert);
    }

    void destroy(pointer p) {
        p->~value_type();
    }

    size_type max_size() const noexcept {
        return size_type(-1) / sizeof(value_type);
    }

    template <typename T2>
    struct rebind {
        using other = AlignmentAllocator<T2, Align>;
    };

    bool operator!=(const AlignmentAllocator<T, Align>& other) const noexcept {
        return !(*this == other);
    }

    // Returns true if and only if storage allocated from *this
    // can be deallocated from other, and vice versa.
    // Always returns true for stateless allocators.
    bool operator==(const AlignmentAllocator<T, Align>& other) const noexcept {
        return true;
    }
};

} // namespace Common
