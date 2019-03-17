// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <optional>

#include "common/common_types.h"

namespace Common {

/**
 * Memory hooks have two purposes:
 * 1. To allow reads and writes to a region of memory to be intercepted. This is used to implement
 *    texture forwarding and memory breakpoints for debugging.
 * 2. To allow for the implementation of MMIO devices.
 *
 * A hook may be mapped to multiple regions of memory.
 *
 * If a std::nullopt or false is returned from a function, the read/write request is passed through
 * to the underlying memory region.
 */
class MemoryHook {
public:
    virtual ~MemoryHook();

    virtual std::optional<bool> IsValidAddress(VAddr addr) = 0;

    virtual std::optional<u8> Read8(VAddr addr) = 0;
    virtual std::optional<u16> Read16(VAddr addr) = 0;
    virtual std::optional<u32> Read32(VAddr addr) = 0;
    virtual std::optional<u64> Read64(VAddr addr) = 0;

    virtual bool ReadBlock(VAddr src_addr, void* dest_buffer, std::size_t size) = 0;

    virtual bool Write8(VAddr addr, u8 data) = 0;
    virtual bool Write16(VAddr addr, u16 data) = 0;
    virtual bool Write32(VAddr addr, u32 data) = 0;
    virtual bool Write64(VAddr addr, u64 data) = 0;

    virtual bool WriteBlock(VAddr dest_addr, const void* src_buffer, std::size_t size) = 0;
};

using MemoryHookPointer = std::shared_ptr<MemoryHook>;
} // namespace Common
