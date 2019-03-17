// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/page_table.h"

namespace Common {

PageTable::PageTable(std::size_t page_size_in_bits) : page_size_in_bits{page_size_in_bits} {}

PageTable::~PageTable() = default;

void PageTable::Resize(std::size_t address_space_width_in_bits) {
    const std::size_t num_page_table_entries = 1ULL
                                               << (address_space_width_in_bits - page_size_in_bits);

    pointers.resize(num_page_table_entries);
    attributes.resize(num_page_table_entries);

    // The default is a 39-bit address space, which causes an initial 1GB allocation size. If the
    // vector size is subsequently decreased (via resize), the vector might not automatically
    // actually reallocate/resize its underlying allocation, which wastes up to ~800 MB for
    // 36-bit titles. Call shrink_to_fit to reduce capacity to what's actually in use.

    pointers.shrink_to_fit();
    attributes.shrink_to_fit();
}

} // namespace Common
