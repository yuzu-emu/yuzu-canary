// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/settings.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/engines/maxwell_dma.h"
#include "video_core/memory_manager.h"
#include "video_core/renderer_base.h"
#include "video_core/textures/decoders.h"

namespace Tegra::Engines {

MaxwellDMA::MaxwellDMA(Core::System& system, MemoryManager& memory_manager)
    : system{system}, memory_manager{memory_manager} {}

void MaxwellDMA::CallMethod(const GPU::MethodCall& method_call) {
    ASSERT_MSG(method_call.method < Regs::NUM_REGS,
               "Invalid MaxwellDMA register, increase the size of the Regs structure");

    regs.reg_array[method_call.method] = method_call.argument;

#define MAXWELLDMA_REG_INDEX(field_name)                                                           \
    (offsetof(Tegra::Engines::MaxwellDMA::Regs, field_name) / sizeof(u32))

    switch (method_call.method) {
    case MAXWELLDMA_REG_INDEX(exec): {
        HandleCopy();
        break;
    }
    }

#undef MAXWELLDMA_REG_INDEX
}

void MaxwellDMA::HandleCopy() {
    LOG_TRACE(HW_GPU, "Requested a DMA copy");

    const GPUVAddr source = regs.src_address.Address();
    const GPUVAddr dest = regs.dst_address.Address();

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
    system.GPU().Maxwell3D().dirty.OnMemoryWrite();

    if (regs.exec.is_dst_linear && regs.exec.is_src_linear) {
        // When the enable_2d bit is disabled, the copy is performed as if we were copying a 1D
        // buffer of length `x_count`, otherwise we copy a 2D image of dimensions (x_count,
        // y_count).
        if (!regs.exec.enable_2d) {
            memory_manager.CopyBlock(dest, source, regs.x_count);
            return;
        }

        // If both the source and the destination are in linear layout, perform a line-by-line
        // copy. We're going to take a subrect of size (x_count, y_count) from the source
        // rectangle. There is no need to manually flush/invalidate the regions because
        // CopyBlock does that for us.
        for (u32 line = 0; line < regs.y_count; ++line) {
            const GPUVAddr source_line = source + line * regs.src_pitch;
            const GPUVAddr dest_line = dest + line * regs.dst_pitch;
            memory_manager.CopyBlock(dest_line, source_line, regs.x_count);
        }
        return;
    }

    ASSERT(regs.exec.enable_2d == 1);

    if (regs.exec.is_dst_linear && !regs.exec.is_src_linear) {
        ASSERT(regs.src_params.BlockDepth() == 0);
        // If the input is tiled and the output is linear, deswizzle the input and copy it over.
        const u32 bytes_per_pixel = regs.dst_pitch / regs.x_count;
        const std::size_t src_size = Texture::CalculateSize(
            true, bytes_per_pixel, regs.src_params.size_x, regs.src_params.size_y,
            regs.src_params.size_z, regs.src_params.BlockHeight(), regs.src_params.BlockDepth());

        const std::size_t src_layer_size = Texture::CalculateSize(
            true, bytes_per_pixel, regs.src_params.size_x, regs.src_params.size_y, 1,
            regs.src_params.BlockHeight(), regs.src_params.BlockDepth());

        const std::size_t dst_size = regs.dst_pitch * regs.y_count;

        if (read_buffer.size() < src_size) {
            read_buffer.resize(src_size);
        }

        if (write_buffer.size() < dst_size) {
            write_buffer.resize(dst_size);
        }

        memory_manager.ReadBlock(source, read_buffer.data(), src_size);
        memory_manager.ReadBlock(dest, write_buffer.data(), dst_size);

        Texture::UnswizzleSubrect(
            regs.x_count, regs.y_count, regs.dst_pitch, regs.src_params.size_x, bytes_per_pixel,
            read_buffer.data() + src_layer_size * regs.src_params.pos_z, write_buffer.data(),
            regs.src_params.BlockHeight(), regs.src_params.pos_x, regs.src_params.pos_y);

        memory_manager.WriteBlock(dest, write_buffer.data(), dst_size);
    } else {
        ASSERT(regs.dst_params.BlockDepth() == 0);

        const u32 bytes_per_pixel = regs.src_pitch / regs.x_count;

        const std::size_t dst_size = Texture::CalculateSize(
            true, bytes_per_pixel, regs.dst_params.size_x, regs.dst_params.size_y,
            regs.dst_params.size_z, regs.dst_params.BlockHeight(), regs.dst_params.BlockDepth());

        const std::size_t dst_layer_size = Texture::CalculateSize(
            true, bytes_per_pixel, regs.dst_params.size_x, regs.dst_params.size_y, 1,
            regs.dst_params.BlockHeight(), regs.dst_params.BlockDepth());

        const std::size_t src_size = regs.src_pitch * regs.y_count;

        if (read_buffer.size() < src_size) {
            read_buffer.resize(src_size);
        }

        if (write_buffer.size() < dst_size) {
            write_buffer.resize(dst_size);
        }

        if (Settings::values.use_accurate_gpu_emulation) {
            memory_manager.ReadBlock(source, read_buffer.data(), src_size);
            memory_manager.ReadBlock(dest, write_buffer.data(), dst_size);
        } else {
            memory_manager.ReadBlockUnsafe(source, read_buffer.data(), src_size);
            memory_manager.ReadBlockUnsafe(dest, write_buffer.data(), dst_size);
        }

        // If the input is linear and the output is tiled, swizzle the input and copy it over.
        Texture::SwizzleSubrect(
            regs.x_count, regs.y_count, regs.src_pitch, regs.dst_params.size_x, bytes_per_pixel,
            write_buffer.data() + dst_layer_size * regs.dst_params.pos_z, read_buffer.data(),
            regs.dst_params.BlockHeight(), regs.dst_params.pos_x, regs.dst_params.pos_y);

        memory_manager.WriteBlock(dest, write_buffer.data(), dst_size);
    }
}

} // namespace Tegra::Engines
