// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/core.h"
#include "core/memory.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/engines/maxwell_dma.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/textures/decoders.h"

namespace Tegra::Engines {

MaxwellDMA::MaxwellDMA(VideoCore::RasterizerInterface& rasterizer, MemoryManager& memory_manager)
    : memory_manager(memory_manager), rasterizer{rasterizer} {}

void MaxwellDMA::WriteReg(u32 method, u32 value) {
    ASSERT_MSG(method < Regs::NUM_REGS,
               "Invalid MaxwellDMA register, increase the size of the Regs structure");

    regs.reg_array[method] = value;

#define MAXWELLDMA_REG_INDEX(field_name)                                                           \
    (offsetof(Tegra::Engines::MaxwellDMA::Regs, field_name) / sizeof(u32))

    switch (method) {
    case MAXWELLDMA_REG_INDEX(exec): {
        HandleCopy();
        break;
    }
    }

#undef MAXWELLDMA_REG_INDEX
}

void MaxwellDMA::HandleCopy() {
    LOG_WARNING(HW_GPU, "Requested a DMA copy");

    const GPUVAddr source = regs.src_address.Address();
    const GPUVAddr dest = regs.dst_address.Address();

    const VAddr source_cpu = *memory_manager.GpuToCpuAddress(source);
    const VAddr dest_cpu = *memory_manager.GpuToCpuAddress(dest);

    // TODO(Subv): Perform more research and implement all features of this engine.
    ASSERT(regs.exec.enable_swizzle == 0);
    ASSERT(regs.exec.query_mode == Regs::QueryMode::None);
    ASSERT(regs.exec.query_intr == Regs::QueryIntr::None);
    ASSERT(regs.exec.copy_mode == Regs::CopyMode::Unk2);
    ASSERT(regs.dst_params.pos_x == 0);
    ASSERT(regs.dst_params.pos_y == 0);

    if (!regs.exec.is_dst_linear && !regs.exec.is_src_linear) {
        // If both the source and the destination are in block layout, assert.
        UNREACHABLE_MSG("Tiled->Tiled DMA transfers are not yet implemented");
        return;
    }

    // All copies here update the main memory, so mark all rasterizer states as invalid.
    Core::System::GetInstance().GPU().Maxwell3D().dirty_flags.OnMemoryWrite();

    if (regs.exec.is_dst_linear && regs.exec.is_src_linear) {
        // When the enable_2d bit is disabled, the copy is performed as if we were copying a 1D
        // buffer of length `x_count`, otherwise we copy a 2D image of dimensions (x_count,
        // y_count).
        if (!regs.exec.enable_2d) {
            Memory::CopyBlock(dest_cpu, source_cpu, regs.x_count);
            return;
        }

        // If both the source and the destination are in linear layout, perform a line-by-line
        // copy. We're going to take a subrect of size (x_count, y_count) from the source
        // rectangle. There is no need to manually flush/invalidate the regions because
        // CopyBlock does that for us.
        for (u32 line = 0; line < regs.y_count; ++line) {
            const VAddr source_line = source_cpu + line * regs.src_pitch;
            const VAddr dest_line = dest_cpu + line * regs.dst_pitch;
            Memory::CopyBlock(dest_line, source_line, regs.x_count);
        }
        return;
    }

    ASSERT(regs.exec.enable_2d == 1);

    const std::size_t copy_size = regs.x_count * regs.y_count;

    const auto FlushAndInvalidate = [&](u32 src_size, u64 dst_size) {
        // TODO(Subv): For now, manually flush the regions until we implement GPU-accelerated
        // copying.
        rasterizer.FlushRegion(source_cpu, src_size);

        // We have to invalidate the destination region to evict any outdated surfaces from the
        // cache. We do this before actually writing the new data because the destination address
        // might contain a dirty surface that will have to be written back to memory.
        rasterizer.InvalidateRegion(dest_cpu, dst_size);
    };

    if (regs.exec.is_dst_linear && !regs.exec.is_src_linear) {
        ASSERT(regs.src_params.size_z == 1);
        // If the input is tiled and the output is linear, deswizzle the input and copy it over.

        const u32 src_bytes_per_pixel = regs.src_pitch / regs.src_params.size_x;

        FlushAndInvalidate(regs.src_pitch * regs.src_params.size_y,
                           copy_size * src_bytes_per_pixel);

        Texture::UnswizzleSubrect(regs.x_count, regs.y_count, regs.dst_pitch,
                                  regs.src_params.size_x, src_bytes_per_pixel, source_cpu, dest_cpu,
                                  regs.src_params.BlockHeight(), regs.src_params.pos_x,
                                  regs.src_params.pos_y);
    } else {
        ASSERT(regs.dst_params.size_z == 1);
        ASSERT(regs.src_pitch == regs.x_count);

        const u32 src_bpp = regs.src_pitch / regs.x_count;

        FlushAndInvalidate(regs.src_pitch * regs.y_count,
                           regs.dst_params.size_x * regs.dst_params.size_y * src_bpp);

        // If the input is linear and the output is tiled, swizzle the input and copy it over.
        Texture::SwizzleSubrect(regs.x_count, regs.y_count, regs.src_pitch, regs.dst_params.size_x,
                                src_bpp, dest_cpu, source_cpu, regs.dst_params.BlockHeight());
    }
}

} // namespace Tegra::Engines
