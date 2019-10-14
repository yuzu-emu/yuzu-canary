// Copyright 2016 Dolphin Emulator Project / 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <catch2/catch.hpp>

#include <array>
#include <bitset>
#include <cstdlib>
#include <string>
#include "common/file_util.h"
#include "core/core.h"
#include "core/core_timing.h"

// Numbers are chosen randomly to make sure the correct one is given.
static constexpr std::array<u64, 5> CB_IDS{{42, 144, 93, 1026, UINT64_C(0xFFFF7FFFF7FFFF)}};
static constexpr int MAX_SLICE_LENGTH = 10000; // Copied from CoreTiming internals

static std::bitset<CB_IDS.size()> callbacks_ran_flags;
static u64 expected_callback = 0;
static s64 lateness = 0;

template <unsigned int IDX>
void CallbackTemplate(u64 userdata, s64 cycles_late) {
    static_assert(IDX < CB_IDS.size(), "IDX out of range");
    callbacks_ran_flags.set(IDX);
    REQUIRE(CB_IDS[IDX] == userdata);
    REQUIRE(CB_IDS[IDX] == expected_callback);
    REQUIRE(lateness == cycles_late);
}

static u64 callbacks_done = 0;

void EmptyCallback(u64 userdata, s64 cycles_late) {
    ++callbacks_done;
}

struct ScopeInit final {
    ScopeInit() {
        core_timing.Initialize();
    }
    ~ScopeInit() {
        core_timing.Shutdown();
    }

    Core::Timing::CoreTiming core_timing;
};

static void AdvanceAndCheck(Core::Timing::CoreTiming& core_timing, u32 idx, u32 context = 0,
                            int expected_lateness = 0, int cpu_downcount = 0) {
    callbacks_ran_flags = 0;
    expected_callback = CB_IDS[idx];
    lateness = expected_lateness;

    // Pretend we executed X cycles of instructions.
    core_timing.SwitchContext(context);
    core_timing.AddTicks(core_timing.GetDowncount() - cpu_downcount);
    core_timing.Advance();
    core_timing.SwitchContext((context + 1) % 4);

    REQUIRE(decltype(callbacks_ran_flags)().set(idx) == callbacks_ran_flags);
}

TEST_CASE("CoreTiming[BasicOrder]", "[core]") {
    ScopeInit guard;
    auto& core_timing = guard.core_timing;

    Core::Timing::EventType* cb_a = core_timing.RegisterEvent("callbackA", CallbackTemplate<0>);
    Core::Timing::EventType* cb_b = core_timing.RegisterEvent("callbackB", CallbackTemplate<1>);
    Core::Timing::EventType* cb_c = core_timing.RegisterEvent("callbackC", CallbackTemplate<2>);
    Core::Timing::EventType* cb_d = core_timing.RegisterEvent("callbackD", CallbackTemplate<3>);
    Core::Timing::EventType* cb_e = core_timing.RegisterEvent("callbackE", CallbackTemplate<4>);

    // Enter slice 0
    core_timing.ResetRun();

    // D -> B -> C -> A -> E
    core_timing.SwitchContext(0);
    core_timing.ScheduleEvent(1000, cb_a, CB_IDS[0]);
    REQUIRE(1000 == core_timing.GetDowncount());
    core_timing.ScheduleEvent(500, cb_b, CB_IDS[1]);
    REQUIRE(500 == core_timing.GetDowncount());
    core_timing.ScheduleEvent(800, cb_c, CB_IDS[2]);
    REQUIRE(500 == core_timing.GetDowncount());
    core_timing.ScheduleEvent(100, cb_d, CB_IDS[3]);
    REQUIRE(100 == core_timing.GetDowncount());
    core_timing.ScheduleEvent(1200, cb_e, CB_IDS[4]);
    REQUIRE(100 == core_timing.GetDowncount());

    AdvanceAndCheck(core_timing, 3, 0);
    AdvanceAndCheck(core_timing, 1, 1);
    AdvanceAndCheck(core_timing, 2, 2);
    AdvanceAndCheck(core_timing, 0, 3);
    AdvanceAndCheck(core_timing, 4, 0);
}

TEST_CASE("CoreTiming[FairSharing]", "[core]") {

    ScopeInit guard;
    auto& core_timing = guard.core_timing;

    Core::Timing::EventType* empty_callback =
        core_timing.RegisterEvent("empty_callback", EmptyCallback);

    callbacks_done = 0;
    u64 MAX_CALLBACKS = 10;
    for (std::size_t i = 0; i < 10; i++) {
        core_timing.ScheduleEvent(i * 3333U, empty_callback, 0);
    }

    const s64 advances = MAX_SLICE_LENGTH / 10;
    core_timing.ResetRun();
    u64 current_time = core_timing.GetTicks();
    bool keep_running{};
    do {
        keep_running = false;
        for (u32 active_core = 0; active_core < 4; ++active_core) {
            core_timing.SwitchContext(active_core);
            if (core_timing.CanCurrentContextRun()) {
                core_timing.AddTicks(std::min<s64>(advances, core_timing.GetDowncount()));
                core_timing.Advance();
            }
            keep_running |= core_timing.CanCurrentContextRun();
        }
    } while (keep_running);
    u64 current_time_2 = core_timing.GetTicks();

    REQUIRE(MAX_CALLBACKS == callbacks_done);
    REQUIRE(current_time_2 == current_time + MAX_SLICE_LENGTH * 4);
}

TEST_CASE("Core::Timing[PredictableLateness]", "[core]") {
    ScopeInit guard;
    auto& core_timing = guard.core_timing;

    Core::Timing::EventType* cb_a = core_timing.RegisterEvent("callbackA", CallbackTemplate<0>);
    Core::Timing::EventType* cb_b = core_timing.RegisterEvent("callbackB", CallbackTemplate<1>);

    // Enter slice 0
    core_timing.ResetRun();

    core_timing.ScheduleEvent(100, cb_a, CB_IDS[0]);
    core_timing.ScheduleEvent(200, cb_b, CB_IDS[1]);

    AdvanceAndCheck(core_timing, 0, 0, 10, -10); // (100 - 10)
    AdvanceAndCheck(core_timing, 1, 1, 50, -50);
}

namespace ChainSchedulingTest {
static int reschedules = 0;

static void RescheduleCallback(Core::Timing::CoreTiming& core_timing, u64 userdata,
                               s64 cycles_late) {
    --reschedules;
    REQUIRE(reschedules >= 0);
    REQUIRE(lateness == cycles_late);

    if (reschedules > 0) {
        core_timing.ScheduleEvent(1000, reinterpret_cast<Core::Timing::EventType*>(userdata),
                                  userdata);
    }
}
} // namespace ChainSchedulingTest

TEST_CASE("CoreTiming[ChainScheduling]", "[core]") {
    using namespace ChainSchedulingTest;

    ScopeInit guard;
    auto& core_timing = guard.core_timing;

    Core::Timing::EventType* cb_a = core_timing.RegisterEvent("callbackA", CallbackTemplate<0>);
    Core::Timing::EventType* cb_b = core_timing.RegisterEvent("callbackB", CallbackTemplate<1>);
    Core::Timing::EventType* cb_c = core_timing.RegisterEvent("callbackC", CallbackTemplate<2>);
    Core::Timing::EventType* cb_rs = core_timing.RegisterEvent(
        "callbackReschedule", [&core_timing](u64 userdata, s64 cycles_late) {
            RescheduleCallback(core_timing, userdata, cycles_late);
        });

    // Enter slice 0
    core_timing.ResetRun();

    core_timing.ScheduleEvent(800, cb_a, CB_IDS[0]);
    core_timing.ScheduleEvent(1000, cb_b, CB_IDS[1]);
    core_timing.ScheduleEvent(2200, cb_c, CB_IDS[2]);
    core_timing.ScheduleEvent(1000, cb_rs, reinterpret_cast<u64>(cb_rs));
    REQUIRE(800 == core_timing.GetDowncount());

    reschedules = 3;
    AdvanceAndCheck(core_timing, 0, 0); // cb_a
    AdvanceAndCheck(core_timing, 1, 1); // cb_b, cb_rs
    REQUIRE(2 == reschedules);

    core_timing.AddTicks(core_timing.GetDowncount());
    core_timing.Advance(); // cb_rs
    core_timing.SwitchContext(3);
    REQUIRE(1 == reschedules);
    REQUIRE(200 == core_timing.GetDowncount());

    AdvanceAndCheck(core_timing, 2, 3); // cb_c

    core_timing.AddTicks(core_timing.GetDowncount());
    core_timing.Advance(); // cb_rs
    REQUIRE(0 == reschedules);
}
