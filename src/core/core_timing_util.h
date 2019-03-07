// Copyright 2008 Dolphin Emulator Project / 2017 Citra Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"

namespace Core::Timing {

// The below clock rate is based on Switch's clockspeed being widely known as 1.020GHz
// The exact value used is of course unverified.
constexpr u64 BASE_CLOCK_RATE = 1019215872; // Switch clock speed is 1020MHz un/docked
constexpr u64 CNTFREQ = 19200000;           // Value from fusee.

inline s64 msToCycles(int ms) {
    // since ms is int there is no way to overflow
    return BASE_CLOCK_RATE * static_cast<s64>(ms) / 1000;
}

inline s64 msToCycles(float ms) {
    return static_cast<s64>(BASE_CLOCK_RATE * (0.001f) * ms);
}

inline s64 msToCycles(double ms) {
    return static_cast<s64>(BASE_CLOCK_RATE * (0.001) * ms);
}

inline s64 usToCycles(float us) {
    return static_cast<s64>(BASE_CLOCK_RATE * (0.000001f) * us);
}

inline s64 usToCycles(int us) {
    return (BASE_CLOCK_RATE * static_cast<s64>(us) / 1000000);
}

s64 usToCycles(s64 us);

s64 usToCycles(u64 us);

inline s64 nsToCycles(float ns) {
    return static_cast<s64>(BASE_CLOCK_RATE * (0.000000001f) * ns);
}

inline s64 nsToCycles(int ns) {
    return BASE_CLOCK_RATE * static_cast<s64>(ns) / 1000000000;
}

s64 nsToCycles(s64 ns);

s64 nsToCycles(u64 ns);

inline u64 cyclesToNs(s64 cycles) {
    return cycles * 1000000000 / BASE_CLOCK_RATE;
}

inline s64 cyclesToUs(s64 cycles) {
    return cycles * 1000000 / BASE_CLOCK_RATE;
}

inline u64 cyclesToMs(s64 cycles) {
    return cycles * 1000 / BASE_CLOCK_RATE;
}

u64 CpuCyclesToClockCycles(u64 ticks);

} // namespace Core::Timing
