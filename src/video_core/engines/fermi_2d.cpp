// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/memory.h"
#include "video_core/engines/fermi_2d.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/textures/decoders.h"

namespace Tegra::Engines {

Fermi2D::Fermi2D(VideoCore::RasterizerInterface& rasterizer, MemoryManager& memory_manager)
    : memory_manager(memory_manager), rasterizer{rasterizer} {}

void Fermi2D::WriteReg(u32 method, u32 value) {
    ASSERT_MSG(method < Regs::NUM_REGS,
               "Invalid Fermi2D register, increase the size of the Regs structure");

    regs.reg_array[method] = value;

    switch (method) {
    case FERMI2D_REG_INDEX(trigger): {
        HandleSurfaceCopy();
        break;
    }
    }
}

void Fermi2D::HandleSurfaceCopy() {
    LOG_WARNING(HW_GPU, "Requested a surface copy with operation {}",
                static_cast<u32>(regs.operation));

    const GPUVAddr source = regs.src.Address();
    const GPUVAddr dest = regs.dst.Address();

    // TODO(Subv): Only same-format and same-size copies are allowed for now.
    ASSERT(regs.src.format == regs.dst.format);
    ASSERT(regs.src.width * regs.src.height == regs.dst.width * regs.dst.height);

    // TODO(Subv): Only raw copies are implemented.
    ASSERT(regs.operation == Regs::Operation::SrcCopy);

    const VAddr source_cpu = *memory_manager.GpuToCpuAddress(source);
    const VAddr dest_cpu = *memory_manager.GpuToCpuAddress(dest);

    u32 src_bytes_per_pixel = RenderTargetBytesPerPixel(regs.src.format);
    u32 dst_bytes_per_pixel = RenderTargetBytesPerPixel(regs.dst.format);

    if (!rasterizer.AccelerateSurfaceCopy(regs.src, regs.dst)) {
        // TODO(bunnei): The below implementation currently will not get hit, as
        // AccelerateSurfaceCopy tries to always copy and will always return success. This should be
        // changed once we properly support flushing.

        if (regs.src.linear == regs.dst.linear) {
            // If the input layout and the output layout are the same, just perform a raw copy.
            ASSERT(regs.src.BlockHeight() == regs.dst.BlockHeight());
            Memory::CopyBlock(dest_cpu, source_cpu,
                              src_bytes_per_pixel * regs.dst.width * regs.dst.height);
            return;
        }
        u8* src_buffer = Memory::GetPointer(source_cpu);
        u8* dst_buffer = Memory::GetPointer(dest_cpu);
        if (!regs.src.linear && regs.dst.linear) {
            // If the input is tiled and the output is linear, deswizzle the input and copy it over.
            Texture::CopySwizzledData(regs.src.width, regs.src.height, src_bytes_per_pixel,
                                      dst_bytes_per_pixel, src_buffer, dst_buffer, true,
                                      regs.src.BlockHeight());
        } else {
            // If the input is linear and the output is tiled, swizzle the input and copy it over.
            Texture::CopySwizzledData(regs.src.width, regs.src.height, src_bytes_per_pixel,
                                      dst_bytes_per_pixel, dst_buffer, src_buffer, false,
                                      regs.dst.BlockHeight());
        }
    }
}

} // namespace Tegra::Engines
