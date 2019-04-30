// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "video_core/engines/kepler_compute.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_base.h"
#include "video_core/textures/decoders.h"

namespace Tegra::Engines {

KeplerCompute::KeplerCompute(Core::System& system, VideoCore::RasterizerInterface& rasterizer,
                             MemoryManager& memory_manager)
    : system{system}, rasterizer{rasterizer}, memory_manager{memory_manager}, upload_state{
                                                                                  memory_manager,
                                                                                  regs.upload} {}

KeplerCompute::~KeplerCompute() = default;

void KeplerCompute::CallMethod(const GPU::MethodCall& method_call) {
    ASSERT_MSG(method_call.method < Regs::NUM_REGS,
               "Invalid KeplerCompute register, increase the size of the Regs structure");

    regs.reg_array[method_call.method] = method_call.argument;

    switch (method_call.method) {
    case KEPLER_COMPUTE_REG_INDEX(exec_upload): {
        upload_state.ProcessExec(regs.exec_upload.linear != 0);
        break;
    }
    case KEPLER_COMPUTE_REG_INDEX(data_upload): {
        const bool is_last_call = method_call.IsLastCall();
        upload_state.ProcessData(method_call.argument, is_last_call);
        if (is_last_call) {
            system.GPU().Maxwell3D().dirty_flags.OnMemoryWrite();
        }
        break;
    }
    case KEPLER_COMPUTE_REG_INDEX(launch):
        ProcessLaunch();
        break;
    default:
        break;
    }
}

void KeplerCompute::ProcessLaunch() {

    const GPUVAddr launch_desc_loc = regs.launch_desc_loc.Address();
    memory_manager.ReadBlockUnsafe(launch_desc_loc, &launch_description,
                                   LaunchParams::NUM_LAUNCH_PARAMETERS * sizeof(u32));

    const GPUVAddr code_loc = regs.code_loc.Address() + launch_description.program_start;
    LOG_WARNING(HW_GPU, "Compute Kernel Execute at Address 0x{:016x}, STUBBED", code_loc);
}

} // namespace Tegra::Engines
