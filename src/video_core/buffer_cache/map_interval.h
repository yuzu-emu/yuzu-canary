// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"
#include "video_core/gpu.h"

namespace VideoCommon {

class MapIntervalBase {
public:
    MapIntervalBase(const CacheAddr start, const CacheAddr end, const GPUVAddr gpu_addr)
        : start{start}, end{end}, gpu_addr{gpu_addr} {}

    void SetCpuAddress(VAddr new_cpu_addr) {
        cpu_addr = new_cpu_addr;
    }

    VAddr GetCpuAddress() const {
        return cpu_addr;
    }

    GPUVAddr GetGpuAddress() const {
        return gpu_addr;
    }

    bool IsInside(const CacheAddr other_start, const CacheAddr other_end) const {
        return (start <= other_start && other_end <= end);
    }

    bool operator==(const MapIntervalBase& rhs) const {
        return std::tie(start, end) == std::tie(rhs.start, rhs.end);
    }

    bool operator!=(const MapIntervalBase& rhs) const {
        return !operator==(rhs);
    }

    void MarkAsRegistered(const bool registered) {
        is_registered = registered;
    }

    bool IsRegistered() const {
        return is_registered;
    }

    CacheAddr GetStart() const {
        return start;
    }

    CacheAddr GetEnd() const {
        return end;
    }

    void MarkAsModified(const bool is_modified_, const u64 tick) {
        is_modified = is_modified_;
        ticks = tick;
    }

    bool IsModified() const {
        return is_modified;
    }

    u64 GetModificationTick() const {
        return ticks;
    }

    void MarkAsWritten(const bool is_written_) {
        is_written = is_written_;
    }

    bool IsWritten() const {
        return is_written;
    }

private:
    CacheAddr start;
    CacheAddr end;
    GPUVAddr gpu_addr;
    VAddr cpu_addr{};
    bool is_written{};
    bool is_modified{};
    bool is_registered{};
    u64 ticks{};
};

} // namespace VideoCommon
