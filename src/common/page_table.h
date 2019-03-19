// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <vector>
#include <boost/icl/interval_map.hpp>
#include "common/common_types.h"
#include "common/memory_hook.h"

namespace Common {

enum class PageType : u8 {
    /// Page is unmapped and should cause an access error.
    Unmapped,
    /// Page is mapped to regular memory. This is the only type you can get pointers to.
    Memory,
    /// Page is mapped to regular memory, but also needs to check for rasterizer cache flushing and
    /// invalidation
    RasterizerCachedMemory,
    /// Page is mapped to a I/O region. Writing and reading to this page is handled by functions.
    Special,
    /// Page is allocated for use.
    Allocated,
};

struct SpecialRegion {
    enum class Type {
        DebugHook,
        IODevice,
    } type;

    MemoryHookPointer handler;

    bool operator<(const SpecialRegion& other) const {
        return std::tie(type, handler) < std::tie(other.type, other.handler);
    }

    bool operator==(const SpecialRegion& other) const {
        return std::tie(type, handler) == std::tie(other.type, other.handler);
    }
};

/**
 * A (reasonably) fast way of allowing switchable and remappable process address spaces. It loosely
 * mimics the way a real CPU page table works.
 */
struct PageTable {
    explicit PageTable(std::size_t page_size_in_bits);
    ~PageTable();

    /**
     * Resizes the page table to be able to accomodate enough pages within
     * a given address space.
     *
     * @param address_space_width_in_bits The address size width in bits.
     */
    void Resize(std::size_t address_space_width_in_bits);

    /**
     * Vector of memory pointers backing each page. An entry can only be non-null if the
     * corresponding entry in the `attributes` vector is of type `Memory`.
     */
    std::vector<u8*> pointers;

    /**
     * Contains MMIO handlers that back memory regions whose entries in the `attribute` vector is
     * of type `Special`.
     */
    boost::icl::interval_map<u64, std::set<SpecialRegion>> special_regions;

    /**
     * Vector of fine grained page attributes. If it is set to any value other than `Memory`, then
     * the corresponding entry in `pointers` MUST be set to null.
     */
    std::vector<PageType> attributes;

    std::vector<u64> backing_addr;

    const std::size_t page_size_in_bits{};
};

} // namespace Common
