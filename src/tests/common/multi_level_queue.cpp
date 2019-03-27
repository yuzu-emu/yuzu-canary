// Copyright 2019 Yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <catch2/catch.hpp>
#include <math.h>
#include "common/common_types.h"
#include "common/multi_level_queue.h"

namespace Common {

TEST_CASE("MultiLevelQueue", "[common]") {
    std::array<f32, 8> values = {0.0, 5.0, 1.0, 9.0, 8.0, 2.0, 6.0, 7.0};
    Common::MultiLevelQueue<f32, 64> mlq;
    REQUIRE(mlq.empty());
    mlq.add(values[2], 2);
    mlq.add(values[7], 7);
    mlq.add(values[3], 3);
    mlq.add(values[4], 4);
    mlq.add(values[0], 0);
    mlq.add(values[5], 5);
    mlq.add(values[6], 6);
    mlq.add(values[1], 1);
    u32 index = 0;
    bool all_set = true;
    for (auto& f : mlq) {
        all_set &= (f == values[index]);
        index++;
    }
    REQUIRE(all_set);
    REQUIRE(!mlq.empty());
    f32 v = 8.0;
    mlq.add(v, 2);
    v = -7.0;
    mlq.add(v, 2, false);
    REQUIRE(mlq.front(2) == -7.0);
    mlq.yield(2);
    REQUIRE(mlq.front(2) == values[2]);
    REQUIRE(mlq.back(2) == -7.0);
    REQUIRE(mlq.empty(8));
    v = 10.0;
    mlq.add(v, 8);
    mlq.adjust(v, 8, 9);
    REQUIRE(mlq.front(9) == v);
    REQUIRE(mlq.empty(8));
    REQUIRE(!mlq.empty(9));
    mlq.adjust(values[0], 0, 9);
    REQUIRE(mlq.highest_priority_set() == 1);
    REQUIRE(mlq.lowest_priority_set() == 9);
    mlq.remove(values[1], 1);
    REQUIRE(mlq.highest_priority_set() == 2);
    REQUIRE(mlq.empty(1));
}

} // namespace Common
