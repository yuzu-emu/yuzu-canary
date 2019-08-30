// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <unordered_set>
#include <utility>

#include "common/alignment.h"
#include "common/common_types.h"
#include "video_core/gpu.h"

namespace VideoCommon {

class BufferBlock {
public:
    bool Overlaps(const CacheAddr start, const CacheAddr end) const {
        return (cache_addr < end) && (cache_addr_end > start);
    }

    bool IsInside(const CacheAddr other_start, const CacheAddr other_end) const {
        return cache_addr <= other_start && other_end <= cache_addr_end;
    }

    u8* GetWritableHostPtr() const {
        return FromCacheAddr(cache_addr);
    }

    u8* GetWritableHostPtr(std::size_t offset) const {
        return FromCacheAddr(cache_addr + offset);
    }

    std::size_t GetOffset(const CacheAddr in_addr) {
        return static_cast<std::size_t>(in_addr - cache_addr);
    }

    CacheAddr GetCacheAddr() const {
        return cache_addr;
    }

    CacheAddr GetCacheAddrEnd() const {
        return cache_addr_end;
    }

    void SetCacheAddr(const CacheAddr new_addr) {
        cache_addr = new_addr;
        cache_addr_end = new_addr + size;
    }

    std::size_t GetSize() const {
        return size;
    }

    void SetEpoch(u64 new_epoch) {
        epoch = new_epoch;
    }

    u64 GetEpoch() {
        return epoch;
    }

protected:
    explicit BufferBlock(CacheAddr cache_addr, const std::size_t size) : size{size} {
        SetCacheAddr(cache_addr);
    }
    ~BufferBlock() = default;

private:
    CacheAddr cache_addr{};
    CacheAddr cache_addr_end{};
    std::size_t size{};
    u64 epoch{};
};

} // namespace VideoCommon
