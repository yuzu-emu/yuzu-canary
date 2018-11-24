// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "core/core.h"
#include "video_core/engines/maxwell_compute.h"

namespace Tegra::Engines {

void MaxwellCompute::CallMethod(const GPU::MethodCall& method_call) {
    ASSERT_MSG(method_call.method < Regs::NUM_REGS,
               "Invalid MaxwellCompute register, increase the size of the Regs structure");

    regs.reg_array[method_call.method] = method_call.argument;

    switch (method_call.method) {
    case MAXWELL_COMPUTE_REG_INDEX(compute): {
        LOG_CRITICAL(HW_GPU, "Compute shaders are not implemented");
        UNREACHABLE();
        break;
    }
    default:
        break;
    }
}

} // namespace Tegra::Engines
