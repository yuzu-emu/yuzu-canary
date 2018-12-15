// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/core.h"
#include "core/memory.h"
#include "video_core/engines/fermi_2d.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/textures/decoders.h"

namespace Tegra::Engines {

Fermi2D::Fermi2D(VideoCore::RasterizerInterface& rasterizer, MemoryManager& memory_manager)
    : memory_manager(memory_manager), rasterizer{rasterizer} {}

void Fermi2D::CallMethod(const GPU::MethodCall& method_call) {
    ASSERT_MSG(method_call.method < Regs::NUM_REGS,
               "Invalid Fermi2D register, increase the size of the Regs structure");

    regs.reg_array[method_call.method] = method_call.argument;

    switch (method_call.method) {
    // Trigger the surface copy on the last register write. This is blit_src_y, but this is 64-bit,
    // so trigger on the second 32-bit write.
    case FERMI2D_REG_INDEX(blit_src_y) + 1: {
        HandleSurfaceCopy();
        break;
    }
    }
}

void Fermi2D::HandleSurfaceCopy() {
    LOG_WARNING(HW_GPU, "Requested a surface copy with operation {}",
                static_cast<u32>(regs.operation));

    // TODO(Subv): Only raw copies are implemented.
    ASSERT(regs.operation == Regs::Operation::SrcCopy);

    const u32 src_blit_x1{static_cast<u32>(regs.blit_src_x >> 32)};
    const u32 src_blit_y1{static_cast<u32>(regs.blit_src_y >> 32)};
    const u32 src_blit_x2{
        static_cast<u32>(regs.blit_src_x + regs.blit_dst_width * regs.blit_dud_x >> 32)};
    const u32 src_blit_y2{
        static_cast<u32>(regs.blit_src_y + regs.blit_dst_height * regs.blit_dvd_y >> 32)};

    const MathUtil::Rectangle<u32> src_rect{src_blit_x1, src_blit_y1, src_blit_x2, src_blit_y2};
    const MathUtil::Rectangle<u32> dst_rect{regs.blit_dst_x, regs.blit_dst_y,
                                            regs.blit_dst_x + regs.blit_dst_width,
                                            regs.blit_dst_y + regs.blit_dst_height};

    if (!rasterizer.AccelerateSurfaceCopy(regs.src, regs.dst, src_rect, dst_rect)) {
        UNIMPLEMENTED();
    }
}

} // namespace Tegra::Engines
