// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/memory.h"
#include "video_core/engines/maxwell_dma.h"
#include "video_core/textures/decoders.h"

namespace Tegra {
namespace Engines {

MaxwellDMA::MaxwellDMA(MemoryManager& memory_manager) : memory_manager(memory_manager) {}

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
    ASSERT(regs.src_params.pos_x == 0);
    ASSERT(regs.src_params.pos_y == 0);
    ASSERT(regs.dst_params.pos_x == 0);
    ASSERT(regs.dst_params.pos_y == 0);

    if (regs.exec.is_dst_linear == regs.exec.is_src_linear) {
        std::size_t copy_size = regs.x_count;

        // When the enable_2d bit is disabled, the copy is performed as if we were copying a 1D
        // buffer of length `x_count`, otherwise we copy a 2D buffer of size (x_count, y_count).
        if (regs.exec.enable_2d) {
            copy_size = copy_size * regs.y_count;
        }

        Memory::CopyBlock(dest_cpu, source_cpu, copy_size);
        return;
    }

    ASSERT(regs.exec.enable_2d == 1);
    u8* src_buffer = Memory::GetPointer(source_cpu);
    u8* dst_buffer = Memory::GetPointer(dest_cpu);

    if (regs.exec.is_dst_linear && !regs.exec.is_src_linear) {
        // If the input is tiled and the output is linear, deswizzle the input and copy it over.
        Texture::CopySwizzledData(regs.src_params.size_x, regs.src_params.size_y,
                                  regs.src_params.size_z, 1, 1, src_buffer, dst_buffer, true,
                                  regs.src_params.BlockHeight(), regs.src_params.BlockDepth());
    } else {
        // If the input is linear and the output is tiled, swizzle the input and copy it over.
        Texture::CopySwizzledData(regs.dst_params.size_x, regs.dst_params.size_y,
                                  regs.dst_params.size_z, 1, 1, dst_buffer, src_buffer, false,
                                  regs.dst_params.BlockHeight(), regs.dst_params.BlockDepth());
    }
}

} // namespace Engines
} // namespace Tegra
