// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <catch2/catch.hpp>
#include <math.h>
#include "common/bit_util.h"

namespace Common {

TEST_CASE("BitUtils::CountTrailingZeroes", "[common]") {
    REQUIRE(Common::CountTrailingZeroes32(0) == 32);
    REQUIRE(Common::CountTrailingZeroes64(0) == 64);
    REQUIRE(Common::CountTrailingZeroes32(9) == 0);
    REQUIRE(Common::CountTrailingZeroes32(8) == 3);
    REQUIRE(Common::CountTrailingZeroes32(0x801000) == 12);
    REQUIRE(Common::CountTrailingZeroes64(9) == 0);
    REQUIRE(Common::CountTrailingZeroes64(8) == 3);
    REQUIRE(Common::CountTrailingZeroes64(0x801000) == 12);
    REQUIRE(Common::CountTrailingZeroes64(0x801000000000UL) == 36);
}

} // namespace Common
