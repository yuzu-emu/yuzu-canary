// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/memory.h"
#include "video_core/engines/maxwell_dma.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/textures/decoders.h"

namespace Tegra {
namespace Engines {

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

    size_t copy_size = regs.x_count;

    // When the enable_2d bit is disabled, the copy is performed as if we were copying a 1D
    // buffer of length `x_count`, otherwise we copy a 2D buffer of size (x_count, y_count).
    if (regs.exec.enable_2d) {
        copy_size = copy_size * regs.y_count;
    }

    if (regs.exec.is_dst_linear == regs.exec.is_src_linear) {
        ASSERT(regs.src_params.pos_x == 0);
        ASSERT(regs.src_params.pos_y == 0);
        ASSERT(regs.dst_pitch == 1);
        ASSERT(regs.src_pitch == 1);

        // CopyBlock already takes care of flushing and invalidating the cache at the affected
        // addresses.
        Memory::CopyBlock(dest_cpu, source_cpu, copy_size);
        return;
    }

    ASSERT(regs.exec.enable_2d == 1);

    // TODO(Subv): For now, manually flush the regions until we implement GPU-accelerated copying.
    rasterizer.FlushRegion(source_cpu, copy_size);

    // We have to invalidate the destination region to evict any outdated surfaces from the cache.
    // We do this before actually writing the new data because the destination address might contain
    // a dirty surface that will have to be written back to memory.
    rasterizer.InvalidateRegion(dest_cpu, copy_size);

    u8* src_buffer = Memory::GetPointer(source_cpu);
    u8* dst_buffer = Memory::GetPointer(dest_cpu);

    if (regs.exec.is_dst_linear && !regs.exec.is_src_linear) {
        // If the input is tiled and the output is linear, deswizzle the input and copy it over.

        // Copy the data to a staging buffer first to make applying the src and dst offsets easier
        std::vector<u8> staging_buffer(regs.src_pitch * regs.src_params.size_y);

        // In this mode, the src_pitch register contains the source stride, and the dst_pitch
        // contains the bytes per pixel.
        u32 src_bytes_per_pixel = regs.src_pitch / regs.src_params.size_x;
        u32 dst_bytes_per_pixel = regs.dst_pitch;

        Texture::CopySwizzledData(regs.src_params.size_x, regs.src_params.size_y,
                                  src_bytes_per_pixel, dst_bytes_per_pixel, src_buffer,
                                  staging_buffer.data(), true, regs.src_params.BlockHeight());

        u32 src_offset = (regs.src_params.pos_y * regs.src_params.size_x + regs.src_params.pos_x) *
                         regs.dst_pitch;
        std::memcpy(dst_buffer, staging_buffer.data() + src_offset, copy_size * regs.dst_pitch);
    } else {
        // TODO(Subv): Source offsets are not yet implemented for this mode.
        ASSERT(regs.src_params.pos_x == 0);
        ASSERT(regs.src_params.pos_y == 0);

        // If the input is linear and the output is tiled, swizzle the input and copy it over.
        Texture::CopySwizzledData(regs.dst_params.size_x, regs.dst_params.size_y, 1, 1, dst_buffer,
                                  src_buffer, false, regs.dst_params.BlockHeight());
    }
}

} // namespace Engines
} // namespace Tegra
