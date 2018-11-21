// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include "common/assert.h"
#include "common/logging/log.h"
#include "core/core_timing.h"
#include "core/core_timing_util.h"
#include "core/hle/service/nvdrv/devices/nvhost_ctrl_gpu.h"

namespace Service::Nvidia::Devices {

nvhost_ctrl_gpu::nvhost_ctrl_gpu() = default;
nvhost_ctrl_gpu::~nvhost_ctrl_gpu() = default;

u32 nvhost_ctrl_gpu::ioctl(Ioctl command, const std::vector<u8>& input, std::vector<u8>& output) {
    LOG_DEBUG(Service_NVDRV, "called, command=0x{:08X}, input_size=0x{:X}, output_size=0x{:X}",
              command.raw, input.size(), output.size());

    switch (static_cast<IoctlCommand>(command.raw)) {
    case IoctlCommand::IocGetCharacteristicsCommand:
        return GetCharacteristics(input, output);
    case IoctlCommand::IocGetTPCMasksCommand:
        return GetTPCMasks(input, output);
    case IoctlCommand::IocGetActiveSlotMaskCommand:
        return GetActiveSlotMask(input, output);
    case IoctlCommand::IocZcullGetCtxSizeCommand:
        return ZCullGetCtxSize(input, output);
    case IoctlCommand::IocZcullGetInfo:
        return ZCullGetInfo(input, output);
    case IoctlCommand::IocZbcSetTable:
        return ZBCSetTable(input, output);
    case IoctlCommand::IocZbcQueryTable:
        return ZBCQueryTable(input, output);
    case IoctlCommand::IocFlushL2:
        return FlushL2(input, output);
    case IoctlCommand::IocGetGpuTime:
        return GetGpuTime(input, output);
    }
    UNIMPLEMENTED_MSG("Unimplemented ioctl");
    return 0;
}

u32 nvhost_ctrl_gpu::GetCharacteristics(const std::vector<u8>& input, std::vector<u8>& output) {
    LOG_DEBUG(Service_NVDRV, "called");
    IoctlCharacteristics params{};
    std::memcpy(&params, input.data(), input.size());
    params.gc.arch = 0x120;
    params.gc.impl = 0xb;
    params.gc.rev = 0xa1;
    params.gc.num_gpc = 0x1;
    params.gc.l2_cache_size = 0x40000;
    params.gc.on_board_video_memory_size = 0x0;
    params.gc.num_tpc_per_gpc = 0x2;
    params.gc.bus_type = 0x20;
    params.gc.big_page_size = 0x20000;
    params.gc.compression_page_size = 0x20000;
    params.gc.pde_coverage_bit_count = 0x1B;
    params.gc.available_big_page_sizes = 0x30000;
    params.gc.gpc_mask = 0x1;
    params.gc.sm_arch_sm_version = 0x503;
    params.gc.sm_arch_spa_version = 0x503;
    params.gc.sm_arch_warp_count = 0x80;
    params.gc.gpu_va_bit_count = 0x28;
    params.gc.reserved = 0x0;
    params.gc.flags = 0x55;
    params.gc.twod_class = 0x902D;
    params.gc.threed_class = 0xB197;
    params.gc.compute_class = 0xB1C0;
    params.gc.gpfifo_class = 0xB06F;
    params.gc.inline_to_memory_class = 0xA140;
    params.gc.dma_copy_class = 0xB0B5;
    params.gc.max_fbps_count = 0x1;
    params.gc.fbp_en_mask = 0x0;
    params.gc.max_ltc_per_fbp = 0x2;
    params.gc.max_lts_per_ltc = 0x1;
    params.gc.max_tex_per_tpc = 0x0;
    params.gc.max_gpc_count = 0x1;
    params.gc.rop_l2_en_mask_0 = 0x21D70;
    params.gc.rop_l2_en_mask_1 = 0x0;
    params.gc.chipname = 0x6230326D67;
    params.gc.gr_compbit_store_base_hw = 0x0;
    params.gpu_characteristics_buf_size = 0xA0;
    params.gpu_characteristics_buf_addr = 0xdeadbeef; // Cannot be 0 (UNUSED)
    std::memcpy(output.data(), &params, output.size());
    return 0;
}

u32 nvhost_ctrl_gpu::GetTPCMasks(const std::vector<u8>& input, std::vector<u8>& output) {
    IoctlGpuGetTpcMasksArgs params{};
    std::memcpy(&params, input.data(), input.size());
    LOG_INFO(Service_NVDRV, "called, mask=0x{:X}, mask_buf_addr=0x{:X}", params.mask_buf_size,
             params.mask_buf_addr);
    // TODO(ogniK): Confirm value on hardware
    if (params.mask_buf_size)
        params.tpc_mask_size = 4 * 1; // 4 * num_gpc
    else
        params.tpc_mask_size = 0;
    std::memcpy(output.data(), &params, sizeof(params));
    return 0;
}

u32 nvhost_ctrl_gpu::GetActiveSlotMask(const std::vector<u8>& input, std::vector<u8>& output) {
    LOG_DEBUG(Service_NVDRV, "called");
    IoctlActiveSlotMask params{};
    if (input.size() > 0) {
        std::memcpy(&params, input.data(), input.size());
    }
    params.slot = 0x07;
    params.mask = 0x01;
    std::memcpy(output.data(), &params, output.size());
    return 0;
}

u32 nvhost_ctrl_gpu::ZCullGetCtxSize(const std::vector<u8>& input, std::vector<u8>& output) {
    LOG_DEBUG(Service_NVDRV, "called");
    IoctlZcullGetCtxSize params{};
    if (input.size() > 0) {
        std::memcpy(&params, input.data(), input.size());
    }
    params.size = 0x1;
    std::memcpy(output.data(), &params, output.size());
    return 0;
}

u32 nvhost_ctrl_gpu::ZCullGetInfo(const std::vector<u8>& input, std::vector<u8>& output) {
    LOG_DEBUG(Service_NVDRV, "called");
    IoctlNvgpuGpuZcullGetInfoArgs params{};

    if (input.size() > 0) {
        std::memcpy(&params, input.data(), input.size());
    }

    params.width_align_pixels = 0x20;
    params.height_align_pixels = 0x20;
    params.pixel_squares_by_aliquots = 0x400;
    params.aliquot_total = 0x800;
    params.region_byte_multiplier = 0x20;
    params.region_header_size = 0x20;
    params.subregion_header_size = 0xc0;
    params.subregion_width_align_pixels = 0x20;
    params.subregion_height_align_pixels = 0x40;
    params.subregion_count = 0x10;
    std::memcpy(output.data(), &params, output.size());
    return 0;
}

u32 nvhost_ctrl_gpu::ZBCSetTable(const std::vector<u8>& input, std::vector<u8>& output) {
    LOG_WARNING(Service_NVDRV, "(STUBBED) called");
    IoctlZbcSetTable params{};
    std::memcpy(&params, input.data(), input.size());
    // TODO(ogniK): What does this even actually do?
    std::memcpy(output.data(), &params, output.size());
    return 0;
}

u32 nvhost_ctrl_gpu::ZBCQueryTable(const std::vector<u8>& input, std::vector<u8>& output) {
    LOG_WARNING(Service_NVDRV, "(STUBBED) called");
    IoctlZbcQueryTable params{};
    std::memcpy(&params, input.data(), input.size());
    // TODO : To implement properly
    std::memcpy(output.data(), &params, output.size());
    return 0;
}

u32 nvhost_ctrl_gpu::FlushL2(const std::vector<u8>& input, std::vector<u8>& output) {
    LOG_WARNING(Service_NVDRV, "(STUBBED) called");
    IoctlFlushL2 params{};
    std::memcpy(&params, input.data(), input.size());
    // TODO : To implement properly
    std::memcpy(output.data(), &params, output.size());
    return 0;
}

u32 nvhost_ctrl_gpu::GetGpuTime(const std::vector<u8>& input, std::vector<u8>& output) {
    LOG_DEBUG(Service_NVDRV, "called");
    IoctlGetGpuTime params{};
    std::memcpy(&params, input.data(), input.size());
    params.gpu_time = CoreTiming::cyclesToNs(CoreTiming::GetTicks());
    std::memcpy(output.data(), &params, output.size());
    return 0;
}

} // namespace Service::Nvidia::Devices
