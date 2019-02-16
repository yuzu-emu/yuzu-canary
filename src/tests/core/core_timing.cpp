// Copyright 2016 Dolphin Emulator Project / 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <catch2/catch.hpp>

#include <array>
#include <bitset>
#include <string>
#include "common/file_util.h"
#include "core/core.h"
#include "core/core_timing.h"

// Numbers are chosen randomly to make sure the correct one is given.
static constexpr std::array<u64, 5> CB_IDS{{42, 144, 93, 1026, UINT64_C(0xFFFF7FFFF7FFFF)}};
static constexpr int MAX_SLICE_LENGTH = 20000; // Copied from CoreTiming internals

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

struct ScopeInit final {
    ScopeInit() {
        core_timing.Initialize();
    }
    ~ScopeInit() {
        core_timing.Shutdown();
    }

    Core::Timing::CoreTiming core_timing;
};

static void AdvanceAndCheck(Core::Timing::CoreTiming& core_timing, u32 idx, int downcount,
                            int expected_lateness = 0, int cpu_downcount = 0) {
    callbacks_ran_flags = 0;
    expected_callback = CB_IDS[idx];
    lateness = expected_lateness;

    // Pretend we executed X cycles of instructions.
    core_timing.AddTicks(core_timing.GetDowncount() - cpu_downcount);
    core_timing.Advance();

    REQUIRE(decltype(callbacks_ran_flags)().set(idx) == callbacks_ran_flags);
    REQUIRE(downcount == core_timing.GetDowncount());
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
    core_timing.Advance();

    // D -> B -> C -> A -> E
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

    AdvanceAndCheck(core_timing, 3, 400);
    AdvanceAndCheck(core_timing, 1, 300);
    AdvanceAndCheck(core_timing, 2, 200);
    AdvanceAndCheck(core_timing, 0, 200);
    AdvanceAndCheck(core_timing, 4, MAX_SLICE_LENGTH);
}

TEST_CASE("CoreTiming[Threadsave]", "[core]") {
    ScopeInit guard;
    auto& core_timing = guard.core_timing;

    Core::Timing::EventType* cb_a = core_timing.RegisterEvent("callbackA", CallbackTemplate<0>);
    Core::Timing::EventType* cb_b = core_timing.RegisterEvent("callbackB", CallbackTemplate<1>);
    Core::Timing::EventType* cb_c = core_timing.RegisterEvent("callbackC", CallbackTemplate<2>);
    Core::Timing::EventType* cb_d = core_timing.RegisterEvent("callbackD", CallbackTemplate<3>);
    Core::Timing::EventType* cb_e = core_timing.RegisterEvent("callbackE", CallbackTemplate<4>);

    // Enter slice 0
    core_timing.Advance();

    // D -> B -> C -> A -> E
    core_timing.ScheduleEventThreadsafe(1000, cb_a, CB_IDS[0]);
    // Manually force since ScheduleEventThreadsafe doesn't call it
    core_timing.ForceExceptionCheck(1000);
    REQUIRE(1000 == core_timing.GetDowncount());
    core_timing.ScheduleEventThreadsafe(500, cb_b, CB_IDS[1]);
    // Manually force since ScheduleEventThreadsafe doesn't call it
    core_timing.ForceExceptionCheck(500);
    REQUIRE(500 == core_timing.GetDowncount());
    core_timing.ScheduleEventThreadsafe(800, cb_c, CB_IDS[2]);
    // Manually force since ScheduleEventThreadsafe doesn't call it
    core_timing.ForceExceptionCheck(800);
    REQUIRE(500 == core_timing.GetDowncount());
    core_timing.ScheduleEventThreadsafe(100, cb_d, CB_IDS[3]);
    // Manually force since ScheduleEventThreadsafe doesn't call it
    core_timing.ForceExceptionCheck(100);
    REQUIRE(100 == core_timing.GetDowncount());
    core_timing.ScheduleEventThreadsafe(1200, cb_e, CB_IDS[4]);
    // Manually force since ScheduleEventThreadsafe doesn't call it
    core_timing.ForceExceptionCheck(1200);
    REQUIRE(100 == core_timing.GetDowncount());

    AdvanceAndCheck(core_timing, 3, 400);
    AdvanceAndCheck(core_timing, 1, 300);
    AdvanceAndCheck(core_timing, 2, 200);
    AdvanceAndCheck(core_timing, 0, 200);
    AdvanceAndCheck(core_timing, 4, MAX_SLICE_LENGTH);
}

namespace SharedSlotTest {
static unsigned int counter = 0;

template <unsigned int ID>
void FifoCallback(u64 userdata, s64 cycles_late) {
    static_assert(ID < CB_IDS.size(), "ID out of range");
    callbacks_ran_flags.set(ID);
    REQUIRE(CB_IDS[ID] == userdata);
    REQUIRE(ID == counter);
    REQUIRE(lateness == cycles_late);
    ++counter;
}
} // namespace SharedSlotTest

TEST_CASE("CoreTiming[SharedSlot]", "[core]") {
    using namespace SharedSlotTest;

    ScopeInit guard;
    auto& core_timing = guard.core_timing;

    Core::Timing::EventType* cb_a = core_timing.RegisterEvent("callbackA", FifoCallback<0>);
    Core::Timing::EventType* cb_b = core_timing.RegisterEvent("callbackB", FifoCallback<1>);
    Core::Timing::EventType* cb_c = core_timing.RegisterEvent("callbackC", FifoCallback<2>);
    Core::Timing::EventType* cb_d = core_timing.RegisterEvent("callbackD", FifoCallback<3>);
    Core::Timing::EventType* cb_e = core_timing.RegisterEvent("callbackE", FifoCallback<4>);

    core_timing.ScheduleEvent(1000, cb_a, CB_IDS[0]);
    core_timing.ScheduleEvent(1000, cb_b, CB_IDS[1]);
    core_timing.ScheduleEvent(1000, cb_c, CB_IDS[2]);
    core_timing.ScheduleEvent(1000, cb_d, CB_IDS[3]);
    core_timing.ScheduleEvent(1000, cb_e, CB_IDS[4]);

    // Enter slice 0
    core_timing.Advance();
    REQUIRE(1000 == core_timing.GetDowncount());

    callbacks_ran_flags = 0;
    counter = 0;
    lateness = 0;
    core_timing.AddTicks(core_timing.GetDowncount());
    core_timing.Advance();
    REQUIRE(MAX_SLICE_LENGTH == core_timing.GetDowncount());
    REQUIRE(0x1FULL == callbacks_ran_flags.to_ullong());
}

TEST_CASE("Core::Timing[PredictableLateness]", "[core]") {
    ScopeInit guard;
    auto& core_timing = guard.core_timing;

    Core::Timing::EventType* cb_a = core_timing.RegisterEvent("callbackA", CallbackTemplate<0>);
    Core::Timing::EventType* cb_b = core_timing.RegisterEvent("callbackB", CallbackTemplate<1>);

    // Enter slice 0
    core_timing.Advance();

    core_timing.ScheduleEvent(100, cb_a, CB_IDS[0]);
    core_timing.ScheduleEvent(200, cb_b, CB_IDS[1]);

    AdvanceAndCheck(core_timing, 0, 90, 10, -10); // (100 - 10)
    AdvanceAndCheck(core_timing, 1, MAX_SLICE_LENGTH, 50, -50);
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
    core_timing.Advance();

    core_timing.ScheduleEvent(800, cb_a, CB_IDS[0]);
    core_timing.ScheduleEvent(1000, cb_b, CB_IDS[1]);
    core_timing.ScheduleEvent(2200, cb_c, CB_IDS[2]);
    core_timing.ScheduleEvent(1000, cb_rs, reinterpret_cast<u64>(cb_rs));
    REQUIRE(800 == core_timing.GetDowncount());

    reschedules = 3;
    AdvanceAndCheck(core_timing, 0, 200);  // cb_a
    AdvanceAndCheck(core_timing, 1, 1000); // cb_b, cb_rs
    REQUIRE(2 == reschedules);

    core_timing.AddTicks(core_timing.GetDowncount());
    core_timing.Advance(); // cb_rs
    REQUIRE(1 == reschedules);
    REQUIRE(200 == core_timing.GetDowncount());

    AdvanceAndCheck(core_timing, 2, 800); // cb_c

    core_timing.AddTicks(core_timing.GetDowncount());
    core_timing.Advance(); // cb_rs
    REQUIRE(0 == reschedules);
    REQUIRE(MAX_SLICE_LENGTH == core_timing.GetDowncount());
}
