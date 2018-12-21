// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"
#include "core/arm/arm_interface.h"
#include "core/core.h"
#include "core/hle/result.h"

namespace Kernel {

static inline u64 Param(int n) {
    return Core::CurrentArmInterface().GetReg(n);
}

/**
 * HLE a function return from the current ARM userland process
 * @param res Result to return
 */
static inline void FuncReturn(u64 res) {
    Core::CurrentArmInterface().SetReg(0, res);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Function wrappers that return type ResultCode

template <ResultCode func(u64)>
void SvcWrap() {
    FuncReturn(func(Param(0)).raw);
}

template <ResultCode func(u32)>
void SvcWrap() {
    FuncReturn(func(static_cast<u32>(Param(0))).raw);
}

template <ResultCode func(u32, u32)>
void SvcWrap() {
    FuncReturn(func(static_cast<u32>(Param(0)), static_cast<u32>(Param(1))).raw);
}

template <ResultCode func(u32*)>
void SvcWrap() {
    u32 param = 0;
    const u32 retval = func(&param).raw;
    Core::CurrentArmInterface().SetReg(1, param);
    FuncReturn(retval);
}

template <ResultCode func(u32*, u32)>
void SvcWrap() {
    u32 param_1 = 0;
    u32 retval = func(&param_1, static_cast<u32>(Param(1))).raw;
    Core::CurrentArmInterface().SetReg(1, param_1);
    FuncReturn(retval);
}

template <ResultCode func(u32*, u32*)>
void SvcWrap() {
    u32 param_1 = 0;
    u32 param_2 = 0;
    const u32 retval = func(&param_1, &param_2).raw;

    auto& arm_interface = Core::CurrentArmInterface();
    arm_interface.SetReg(1, param_1);
    arm_interface.SetReg(2, param_2);

    FuncReturn(retval);
}

template <ResultCode func(u32*, u64)>
void SvcWrap() {
    u32 param_1 = 0;
    const u32 retval = func(&param_1, Param(1)).raw;
    Core::CurrentArmInterface().SetReg(1, param_1);
    FuncReturn(retval);
}

template <ResultCode func(u64*, u32)>
void SvcWrap() {
    u64 param_1 = 0;
    const u32 retval = func(&param_1, static_cast<u32>(Param(1))).raw;
    Core::CurrentArmInterface().SetReg(1, param_1);
    FuncReturn(retval);
}

template <ResultCode func(u64, s32)>
void SvcWrap() {
    FuncReturn(func(Param(0), static_cast<s32>(Param(1))).raw);
}

template <ResultCode func(u64, u32)>
void SvcWrap() {
    FuncReturn(func(Param(0), static_cast<u32>(Param(1))).raw);
}

template <ResultCode func(u64*, u64)>
void SvcWrap() {
    u64 param_1 = 0;
    u32 retval = func(&param_1, Param(1)).raw;
    Core::CurrentArmInterface().SetReg(1, param_1);
    FuncReturn(retval);
}

template <ResultCode func(u64*, u32, u32)>
void SvcWrap() {
    u64 param_1 = 0;
    u32 retval = func(&param_1, static_cast<u32>(Param(1)), static_cast<u32>(Param(2))).raw;
    Core::CurrentArmInterface().SetReg(1, param_1);
    FuncReturn(retval);
}

template <ResultCode func(u32, u64)>
void SvcWrap() {
    FuncReturn(func(static_cast<u32>(Param(0)), Param(1)).raw);
}

template <ResultCode func(u32, u32, u64)>
void SvcWrap() {
    FuncReturn(func(static_cast<u32>(Param(0)), static_cast<u32>(Param(1)), Param(2)).raw);
}

template <ResultCode func(u32, u32*, u64*)>
void SvcWrap() {
    u32 param_1 = 0;
    u64 param_2 = 0;
    ResultCode retval = func(static_cast<u32>(Param(2)), &param_1, &param_2);
    Core::CurrentArmInterface().SetReg(1, param_1);
    Core::CurrentArmInterface().SetReg(2, param_2);
    FuncReturn(retval.raw);
}

template <ResultCode func(u64, u64, u32, u32)>
void SvcWrap() {
    FuncReturn(
        func(Param(0), Param(1), static_cast<u32>(Param(2)), static_cast<u32>(Param(3))).raw);
}

template <ResultCode func(u64, u64, u32, u64)>
void SvcWrap() {
    FuncReturn(func(Param(0), Param(1), static_cast<u32>(Param(2)), Param(3)).raw);
}

template <ResultCode func(u32, u64, u32)>
void SvcWrap() {
    FuncReturn(func(static_cast<u32>(Param(0)), Param(1), static_cast<u32>(Param(2))).raw);
}

template <ResultCode func(u64, u64, u64)>
void SvcWrap() {
    FuncReturn(func(Param(0), Param(1), Param(2)).raw);
}

template <ResultCode func(u64, u64, u32)>
void SvcWrap() {
    FuncReturn(func(Param(0), Param(1), static_cast<u32>(Param(2))).raw);
}

template <ResultCode func(u32, u64, u64, u32)>
void SvcWrap() {
    FuncReturn(
        func(static_cast<u32>(Param(0)), Param(1), Param(2), static_cast<u32>(Param(3))).raw);
}

template <ResultCode func(u32, u64, u64)>
void SvcWrap() {
    FuncReturn(func(static_cast<u32>(Param(0)), Param(1), Param(2)).raw);
}

template <ResultCode func(u32*, u64, u64, s64)>
void SvcWrap() {
    u32 param_1 = 0;
    ResultCode retval =
        func(&param_1, Param(1), static_cast<u32>(Param(2)), static_cast<s64>(Param(3)));
    Core::CurrentArmInterface().SetReg(1, param_1);
    FuncReturn(retval.raw);
}

template <ResultCode func(u64, u64, u32, s64)>
void SvcWrap() {
    FuncReturn(
        func(Param(0), Param(1), static_cast<u32>(Param(2)), static_cast<s64>(Param(3))).raw);
}

template <ResultCode func(u64*, u64, u64, u64)>
void SvcWrap() {
    u64 param_1 = 0;
    u32 retval = func(&param_1, Param(1), Param(2), Param(3)).raw;
    Core::CurrentArmInterface().SetReg(1, param_1);
    FuncReturn(retval);
}

template <ResultCode func(u32*, u64, u64, u64, u32, s32)>
void SvcWrap() {
    u32 param_1 = 0;
    u32 retval = func(&param_1, Param(1), Param(2), Param(3), static_cast<u32>(Param(4)),
                      static_cast<s32>(Param(5)))
                     .raw;
    Core::CurrentArmInterface().SetReg(1, param_1);
    FuncReturn(retval);
}

template <ResultCode func(u32*, u64, u64, u32)>
void SvcWrap() {
    u32 param_1 = 0;
    u32 retval = func(&param_1, Param(1), Param(2), static_cast<u32>(Param(3))).raw;
    Core::CurrentArmInterface().SetReg(1, param_1);
    FuncReturn(retval);
}

template <ResultCode func(Handle*, u64, u32, u32)>
void SvcWrap() {
    u32 param_1 = 0;
    u32 retval =
        func(&param_1, Param(1), static_cast<u32>(Param(2)), static_cast<u32>(Param(3))).raw;
    Core::CurrentArmInterface().SetReg(1, param_1);
    FuncReturn(retval);
}

template <ResultCode func(u64, u32, s32, s64)>
void SvcWrap() {
    FuncReturn(func(Param(0), static_cast<u32>(Param(1)), static_cast<s32>(Param(2)),
                    static_cast<s64>(Param(3)))
                   .raw);
}

template <ResultCode func(u64, u32, s32, s32)>
void SvcWrap() {
    FuncReturn(func(Param(0), static_cast<u32>(Param(1)), static_cast<s32>(Param(2)),
                    static_cast<s32>(Param(3)))
                   .raw);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Function wrappers that return type u32

template <u32 func()>
void SvcWrap() {
    FuncReturn(func());
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Function wrappers that return type u64

template <u64 func()>
void SvcWrap() {
    FuncReturn(func());
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// Function wrappers that return type void

template <void func()>
void SvcWrap() {
    func();
}

template <void func(s64)>
void SvcWrap() {
    func(static_cast<s64>(Param(0)));
}

template <void func(u64, u64 len)>
void SvcWrap() {
    func(Param(0), Param(1));
}

template <void func(u64, u64, u64)>
void SvcWrap() {
    func(Param(0), Param(1), Param(2));
}

template <void func(u32, u64, u64)>
void SvcWrap() {
    func(static_cast<u32>(Param(0)), Param(1), Param(2));
}

} // namespace Kernel
