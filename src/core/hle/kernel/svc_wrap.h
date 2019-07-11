// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"
#include "core/arm/arm_interface.h"
#include "core/core.h"
#include "core/hle/result.h"

namespace Kernel {

static inline u64 Param(const Core::System& system, int n) {
    return system.CurrentArmInterface().GetReg(n);
}

/**
 * HLE a function return from the current ARM userland process
 * @param system System context
 * @param result Result to return
 */
static inline void FuncReturn(Core::System& system, u64 result) {
    system.CurrentArmInterface().SetReg(0, result);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Function wrappers that return type ResultCode

template <ResultCode func(Core::System&, u64)>
void SvcWrap(Core::System& system) {
    FuncReturn(system, func(system, Param(system, 0)).raw);
}

template <ResultCode func(Core::System&, u64, u64)>
void SvcWrap(Core::System& system) {
    FuncReturn(system, func(system, Param(system, 0), Param(system, 1)).raw);
}

template <ResultCode func(Core::System&, u32)>
void SvcWrap(Core::System& system) {
    FuncReturn(system, func(system, static_cast<u32>(Param(system, 0))).raw);
}

template <ResultCode func(Core::System&, u32, u32)>
void SvcWrap(Core::System& system) {
    FuncReturn(
        system,
        func(system, static_cast<u32>(Param(system, 0)), static_cast<u32>(Param(system, 1))).raw);
}

template <ResultCode func(Core::System&, u32, u64, u64, u64)>
void SvcWrap(Core::System& system) {
    FuncReturn(system, func(system, static_cast<u32>(Param(system, 0)), Param(system, 1),
                            Param(system, 2), Param(system, 3))
                           .raw);
}

template <ResultCode func(Core::System&, u32*)>
void SvcWrap(Core::System& system) {
    u32 param = 0;
    const u32 retval = func(system, &param).raw;
    system.CurrentArmInterface().SetReg(1, param);
    FuncReturn(system, retval);
}

template <ResultCode func(Core::System&, u32*, u32)>
void SvcWrap(Core::System& system) {
    u32 param_1 = 0;
    const u32 retval = func(system, &param_1, static_cast<u32>(Param(system, 1))).raw;
    system.CurrentArmInterface().SetReg(1, param_1);
    FuncReturn(system, retval);
}

template <ResultCode func(Core::System&, u32*, u32*)>
void SvcWrap(Core::System& system) {
    u32 param_1 = 0;
    u32 param_2 = 0;
    const u32 retval = func(system, &param_1, &param_2).raw;

    auto& arm_interface = system.CurrentArmInterface();
    arm_interface.SetReg(1, param_1);
    arm_interface.SetReg(2, param_2);

    FuncReturn(system, retval);
}

template <ResultCode func(Core::System&, u32*, u64)>
void SvcWrap(Core::System& system) {
    u32 param_1 = 0;
    const u32 retval = func(system, &param_1, Param(system, 1)).raw;
    system.CurrentArmInterface().SetReg(1, param_1);
    FuncReturn(system, retval);
}

template <ResultCode func(Core::System&, u32*, u64, u32)>
void SvcWrap(Core::System& system) {
    u32 param_1 = 0;
    const u32 retval =
        func(system, &param_1, Param(system, 1), static_cast<u32>(Param(system, 2))).raw;

    system.CurrentArmInterface().SetReg(1, param_1);
    FuncReturn(system, retval);
}

template <ResultCode func(Core::System&, u64*, u32)>
void SvcWrap(Core::System& system) {
    u64 param_1 = 0;
    const u32 retval = func(system, &param_1, static_cast<u32>(Param(system, 1))).raw;

    system.CurrentArmInterface().SetReg(1, param_1);
    FuncReturn(system, retval);
}

template <ResultCode func(Core::System&, u64, s32)>
void SvcWrap(Core::System& system) {
    FuncReturn(system, func(system, Param(system, 0), static_cast<s32>(Param(system, 1))).raw);
}

template <ResultCode func(Core::System&, u64, u32)>
void SvcWrap(Core::System& system) {
    FuncReturn(system, func(system, Param(system, 0), static_cast<u32>(Param(system, 1))).raw);
}

template <ResultCode func(Core::System&, u64*, u64)>
void SvcWrap(Core::System& system) {
    u64 param_1 = 0;
    const u32 retval = func(system, &param_1, Param(system, 1)).raw;

    system.CurrentArmInterface().SetReg(1, param_1);
    FuncReturn(system, retval);
}

template <ResultCode func(Core::System&, u64*, u32, u32)>
void SvcWrap(Core::System& system) {
    u64 param_1 = 0;
    const u32 retval = func(system, &param_1, static_cast<u32>(Param(system, 1)),
                            static_cast<u32>(Param(system, 2)))
                           .raw;

    system.CurrentArmInterface().SetReg(1, param_1);
    FuncReturn(system, retval);
}

template <ResultCode func(Core::System&, u32, u64)>
void SvcWrap(Core::System& system) {
    FuncReturn(system, func(system, static_cast<u32>(Param(system, 0)), Param(system, 1)).raw);
}

template <ResultCode func(Core::System&, u32, u32, u64)>
void SvcWrap(Core::System& system) {
    FuncReturn(system, func(system, static_cast<u32>(Param(system, 0)),
                            static_cast<u32>(Param(system, 1)), Param(system, 2))
                           .raw);
}

template <ResultCode func(Core::System&, u32, u32*, u64*)>
void SvcWrap(Core::System& system) {
    u32 param_1 = 0;
    u64 param_2 = 0;
    const ResultCode retval = func(system, static_cast<u32>(Param(system, 2)), &param_1, &param_2);

    system.CurrentArmInterface().SetReg(1, param_1);
    system.CurrentArmInterface().SetReg(2, param_2);
    FuncReturn(system, retval.raw);
}

template <ResultCode func(Core::System&, u64, u64, u32, u32)>
void SvcWrap(Core::System& system) {
    FuncReturn(system, func(system, Param(system, 0), Param(system, 1),
                            static_cast<u32>(Param(system, 2)), static_cast<u32>(Param(system, 3)))
                           .raw);
}

template <ResultCode func(Core::System&, u64, u64, u32, u64)>
void SvcWrap(Core::System& system) {
    FuncReturn(system, func(system, Param(system, 0), Param(system, 1),
                            static_cast<u32>(Param(system, 2)), Param(system, 3))
                           .raw);
}

template <ResultCode func(Core::System&, u32, u64, u32)>
void SvcWrap(Core::System& system) {
    FuncReturn(system, func(system, static_cast<u32>(Param(system, 0)), Param(system, 1),
                            static_cast<u32>(Param(system, 2)))
                           .raw);
}

template <ResultCode func(Core::System&, u64, u64, u64)>
void SvcWrap(Core::System& system) {
    FuncReturn(system, func(system, Param(system, 0), Param(system, 1), Param(system, 2)).raw);
}

template <ResultCode func(Core::System&, u64, u64, u32)>
void SvcWrap(Core::System& system) {
    FuncReturn(
        system,
        func(system, Param(system, 0), Param(system, 1), static_cast<u32>(Param(system, 2))).raw);
}

template <ResultCode func(Core::System&, u32, u64, u64, u32)>
void SvcWrap(Core::System& system) {
    FuncReturn(system, func(system, static_cast<u32>(Param(system, 0)), Param(system, 1),
                            Param(system, 2), static_cast<u32>(Param(system, 3)))
                           .raw);
}

template <ResultCode func(Core::System&, u32, u64, u64)>
void SvcWrap(Core::System& system) {
    FuncReturn(
        system,
        func(system, static_cast<u32>(Param(system, 0)), Param(system, 1), Param(system, 2)).raw);
}

template <ResultCode func(Core::System&, u32*, u64, u64, s64)>
void SvcWrap(Core::System& system) {
    u32 param_1 = 0;
    const u32 retval = func(system, &param_1, Param(system, 1), static_cast<u32>(Param(system, 2)),
                            static_cast<s64>(Param(system, 3)))
                           .raw;

    system.CurrentArmInterface().SetReg(1, param_1);
    FuncReturn(system, retval);
}

template <ResultCode func(Core::System&, u64, u64, u32, s64)>
void SvcWrap(Core::System& system) {
    FuncReturn(system, func(system, Param(system, 0), Param(system, 1),
                            static_cast<u32>(Param(system, 2)), static_cast<s64>(Param(system, 3)))
                           .raw);
}

template <ResultCode func(Core::System&, u64*, u64, u64, u64)>
void SvcWrap(Core::System& system) {
    u64 param_1 = 0;
    const u32 retval =
        func(system, &param_1, Param(system, 1), Param(system, 2), Param(system, 3)).raw;

    system.CurrentArmInterface().SetReg(1, param_1);
    FuncReturn(system, retval);
}

template <ResultCode func(Core::System&, u32*, u64, u64, u64, u32, s32)>
void SvcWrap(Core::System& system) {
    u32 param_1 = 0;
    const u32 retval = func(system, &param_1, Param(system, 1), Param(system, 2), Param(system, 3),
                            static_cast<u32>(Param(system, 4)), static_cast<s32>(Param(system, 5)))
                           .raw;

    system.CurrentArmInterface().SetReg(1, param_1);
    FuncReturn(system, retval);
}

template <ResultCode func(Core::System&, u32*, u64, u64, u32)>
void SvcWrap(Core::System& system) {
    u32 param_1 = 0;
    const u32 retval = func(system, &param_1, Param(system, 1), Param(system, 2),
                            static_cast<u32>(Param(system, 3)))
                           .raw;

    system.CurrentArmInterface().SetReg(1, param_1);
    FuncReturn(system, retval);
}

template <ResultCode func(Core::System&, Handle*, u64, u32, u32)>
void SvcWrap(Core::System& system) {
    u32 param_1 = 0;
    const u32 retval = func(system, &param_1, Param(system, 1), static_cast<u32>(Param(system, 2)),
                            static_cast<u32>(Param(system, 3)))
                           .raw;

    system.CurrentArmInterface().SetReg(1, param_1);
    FuncReturn(system, retval);
}

template <ResultCode func(Core::System&, u64, u32, s32, s64)>
void SvcWrap(Core::System& system) {
    FuncReturn(system, func(system, Param(system, 0), static_cast<u32>(Param(system, 1)),
                            static_cast<s32>(Param(system, 2)), static_cast<s64>(Param(system, 3)))
                           .raw);
}

template <ResultCode func(Core::System&, u64, u32, s32, s32)>
void SvcWrap(Core::System& system) {
    FuncReturn(system, func(system, Param(system, 0), static_cast<u32>(Param(system, 1)),
                            static_cast<s32>(Param(system, 2)), static_cast<s32>(Param(system, 3)))
                           .raw);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Function wrappers that return type u32

template <u32 func(Core::System&)>
void SvcWrap(Core::System& system) {
    FuncReturn(system, func(system));
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Function wrappers that return type u64

template <u64 func(Core::System&)>
void SvcWrap(Core::System& system) {
    FuncReturn(system, func(system));
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// Function wrappers that return type void

template <void func(Core::System&)>
void SvcWrap(Core::System& system) {
    func(system);
}

template <void func(Core::System&, s64)>
void SvcWrap(Core::System& system) {
    func(system, static_cast<s64>(Param(system, 0)));
}

template <void func(Core::System&, u64, u64)>
void SvcWrap(Core::System& system) {
    func(system, Param(system, 0), Param(system, 1));
}

template <void func(Core::System&, u64, u64, u64)>
void SvcWrap(Core::System& system) {
    func(system, Param(system, 0), Param(system, 1), Param(system, 2));
}

template <void func(Core::System&, u32, u64, u64)>
void SvcWrap(Core::System& system) {
    func(system, static_cast<u32>(Param(system, 0)), Param(system, 1), Param(system, 2));
}

} // namespace Kernel
