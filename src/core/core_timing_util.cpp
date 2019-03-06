// Copyright 2008 Dolphin Emulator Project / 2017 Citra Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "core/core_timing_util.h"

#include <cinttypes>
#include <limits>
#include "common/logging/log.h"
#include "common/uint128.h"

namespace Core::Timing {

constexpr u64 MAX_VALUE_TO_MULTIPLY = std::numeric_limits<s64>::max() / BASE_CLOCK_RATE;

s64 usToCycles(s64 us) {
    if (us / 1000000 > MAX_VALUE_TO_MULTIPLY) {
        LOG_ERROR(Core_Timing, "Integer overflow, use max value");
        return std::numeric_limits<s64>::max();
    }
    if (us > MAX_VALUE_TO_MULTIPLY) {
        LOG_DEBUG(Core_Timing, "Time very big, do rounding");
        return BASE_CLOCK_RATE * (us / 1000000);
    }
    return (BASE_CLOCK_RATE * us) / 1000000;
}

s64 usToCycles(u64 us) {
    if (us / 1000000 > MAX_VALUE_TO_MULTIPLY) {
        LOG_ERROR(Core_Timing, "Integer overflow, use max value");
        return std::numeric_limits<s64>::max();
    }
    if (us > MAX_VALUE_TO_MULTIPLY) {
        LOG_DEBUG(Core_Timing, "Time very big, do rounding");
        return BASE_CLOCK_RATE * static_cast<s64>(us / 1000000);
    }
    return (BASE_CLOCK_RATE * static_cast<s64>(us)) / 1000000;
}

s64 nsToCycles(s64 ns) {
    if (ns / 1000000000 > MAX_VALUE_TO_MULTIPLY) {
        LOG_ERROR(Core_Timing, "Integer overflow, use max value");
        return std::numeric_limits<s64>::max();
    }
    if (ns > MAX_VALUE_TO_MULTIPLY) {
        LOG_DEBUG(Core_Timing, "Time very big, do rounding");
        return BASE_CLOCK_RATE * (ns / 1000000000);
    }
    return (BASE_CLOCK_RATE * ns) / 1000000000;
}

s64 nsToCycles(u64 ns) {
    if (ns / 1000000000 > MAX_VALUE_TO_MULTIPLY) {
        LOG_ERROR(Core_Timing, "Integer overflow, use max value");
        return std::numeric_limits<s64>::max();
    }
    if (ns > MAX_VALUE_TO_MULTIPLY) {
        LOG_DEBUG(Core_Timing, "Time very big, do rounding");
        return BASE_CLOCK_RATE * (static_cast<s64>(ns) / 1000000000);
    }
    return (BASE_CLOCK_RATE * static_cast<s64>(ns)) / 1000000000;
}

u64 CpuCyclesToClockCycles(u64 ticks) {
    const u128 temporal = Common::Multiply64Into128(ticks, CNTFREQ);
    return Common::Divide128On32(temporal, static_cast<u32>(BASE_CLOCK_RATE)).first;
}

} // namespace Core::Timing
