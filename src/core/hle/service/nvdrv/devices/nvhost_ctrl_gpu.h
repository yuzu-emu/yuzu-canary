// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <vector>
#include "common/common_types.h"
#include "common/swap.h"
#include "core/hle/service/nvdrv/devices/nvdevice.h"

namespace Service::Nvidia::Devices {

class nvhost_ctrl_gpu final : public nvdevice {
public:
    nvhost_ctrl_gpu();
    ~nvhost_ctrl_gpu() override;

    u32 ioctl(Ioctl command, const std::vector<u8>& input, std::vector<u8>& output) override;

private:
    enum class IoctlCommand : u32_le {
        IocGetCharacteristicsCommand = 0xC0B04705,
        IocGetTPCMasksCommand = 0xC0184706,
        IocGetActiveSlotMaskCommand = 0x80084714,
        IocZcullGetCtxSizeCommand = 0x80044701,
        IocZcullGetInfo = 0x80284702,
        IocZbcSetTable = 0x402C4703,
        IocZbcQueryTable = 0xC0344704,
        IocFlushL2 = 0x40084707,
        IocInvalICache = 0x4008470D,
        IocSetMmudebugMode = 0x4008470E,
        IocSetSmDebugMode = 0x4010470F,
        IocWaitForPause = 0xC0084710,
        IocGetTcpExceptionEnStatus = 0x80084711,
        IocNumVsms = 0x80084712,
        IocVsmsMapping = 0xC0044713,
        IocGetErrorChannelUserData = 0xC008471B,
        IocGetGpuTime = 0xC010471C,
        IocGetCpuTimeCorrelationInfo = 0xC108471D,
    };

    struct IoctlGpuCharacteristics {
        u32_le arch;                       // 0x120 (NVGPU_GPU_ARCH_GM200)
        u32_le impl;                       // 0xB (NVGPU_GPU_IMPL_GM20B)
        u32_le rev;                        // 0xA1 (Revision A1)
        u32_le num_gpc;                    // 0x1
        u64_le l2_cache_size;              // 0x40000
        u64_le on_board_video_memory_size; // 0x0 (not used)
        u32_le num_tpc_per_gpc;            // 0x2
        u32_le bus_type;                   // 0x20 (NVGPU_GPU_BUS_TYPE_AXI)
        u32_le big_page_size;              // 0x20000
        u32_le compression_page_size;      // 0x20000
        u32_le pde_coverage_bit_count;     // 0x1B
        u32_le available_big_page_sizes;   // 0x30000
        u32_le gpc_mask;                   // 0x1
        u32_le sm_arch_sm_version;         // 0x503 (Maxwell Generation 5.0.3?)
        u32_le sm_arch_spa_version;        // 0x503 (Maxwell Generation 5.0.3?)
        u32_le sm_arch_warp_count;         // 0x80
        u32_le gpu_va_bit_count;           // 0x28
        u32_le reserved;                   // NULL
        u64_le flags;                      // 0x55
        u32_le twod_class;                 // 0x902D (FERMI_TWOD_A)
        u32_le threed_class;               // 0xB197 (MAXWELL_B)
        u32_le compute_class;              // 0xB1C0 (MAXWELL_COMPUTE_B)
        u32_le gpfifo_class;               // 0xB06F (MAXWELL_CHANNEL_GPFIFO_A)
        u32_le inline_to_memory_class;     // 0xA140 (KEPLER_INLINE_TO_MEMORY_B)
        u32_le dma_copy_class;             // 0xB0B5 (MAXWELL_DMA_COPY_A)
        u32_le max_fbps_count;             // 0x1
        u32_le fbp_en_mask;                // 0x0 (disabled)
        u32_le max_ltc_per_fbp;            // 0x2
        u32_le max_lts_per_ltc;            // 0x1
        u32_le max_tex_per_tpc;            // 0x0 (not supported)
        u32_le max_gpc_count;              // 0x1
        u32_le rop_l2_en_mask_0;           // 0x21D70 (fuse_status_opt_rop_l2_fbp_r)
        u32_le rop_l2_en_mask_1;           // 0x0
        u64_le chipname;                   // 0x6230326D67 ("gm20b")
        u64_le gr_compbit_store_base_hw;   // 0x0 (not supported)
    };
    static_assert(sizeof(IoctlGpuCharacteristics) == 160,
                  "IoctlGpuCharacteristics is incorrect size");

    struct IoctlCharacteristics {
        u64_le gpu_characteristics_buf_size; // must not be NULL, but gets overwritten with
                                             // 0xA0=max_size
        u64_le gpu_characteristics_buf_addr; // ignored, but must not be NULL
        IoctlGpuCharacteristics gc;
    };
    static_assert(sizeof(IoctlCharacteristics) == 16 + sizeof(IoctlGpuCharacteristics),
                  "IoctlCharacteristics is incorrect size");

    struct IoctlGpuGetTpcMasksArgs {
        /// [in]  TPC mask buffer size reserved by userspace. Should be at least
        /// sizeof(__u32) * fls(gpc_mask) to receive TPC mask for each GPC.
        /// [out] full kernel buffer size
        u32_le mask_buf_size;
        u32_le reserved;

        /// [in]  pointer to TPC mask buffer. It will receive one 32-bit TPC mask per GPC or 0 if
        /// GPC is not enabled or not present. This parameter is ignored if mask_buf_size is 0.
        u64_le mask_buf_addr;
        u64_le tpc_mask_size; // Nintendo add this?
    };
    static_assert(sizeof(IoctlGpuGetTpcMasksArgs) == 24,
                  "IoctlGpuGetTpcMasksArgs is incorrect size");

    struct IoctlActiveSlotMask {
        u32_le slot; // always 0x07
        u32_le mask;
    };
    static_assert(sizeof(IoctlActiveSlotMask) == 8, "IoctlActiveSlotMask is incorrect size");

    struct IoctlZcullGetCtxSize {
        u32_le size;
    };
    static_assert(sizeof(IoctlZcullGetCtxSize) == 4, "IoctlZcullGetCtxSize is incorrect size");

    struct IoctlNvgpuGpuZcullGetInfoArgs {
        u32_le width_align_pixels;
        u32_le height_align_pixels;
        u32_le pixel_squares_by_aliquots;
        u32_le aliquot_total;
        u32_le region_byte_multiplier;
        u32_le region_header_size;
        u32_le subregion_header_size;
        u32_le subregion_width_align_pixels;
        u32_le subregion_height_align_pixels;
        u32_le subregion_count;
    };
    static_assert(sizeof(IoctlNvgpuGpuZcullGetInfoArgs) == 40,
                  "IoctlNvgpuGpuZcullGetInfoArgs is incorrect size");

    struct IoctlZbcSetTable {
        u32_le color_ds[4];
        u32_le color_l2[4];
        u32_le depth;
        u32_le format;
        u32_le type;
    };
    static_assert(sizeof(IoctlZbcSetTable) == 44, "IoctlZbcSetTable is incorrect size");

    struct IoctlZbcQueryTable {
        u32_le color_ds[4];
        u32_le color_l2[4];
        u32_le depth;
        u32_le ref_cnt;
        u32_le format;
        u32_le type;
        u32_le index_size;
    };
    static_assert(sizeof(IoctlZbcQueryTable) == 52, "IoctlZbcQueryTable is incorrect size");

    struct IoctlFlushL2 {
        u32_le flush; // l2_flush | l2_invalidate << 1 | fb_flush << 2
        u32_le reserved;
    };
    static_assert(sizeof(IoctlFlushL2) == 8, "IoctlFlushL2 is incorrect size");

    struct IoctlGetGpuTime {
        u64_le gpu_time;
    };
    static_assert(sizeof(IoctlGetGpuTime) == 8, "IoctlGetGpuTime is incorrect size");

    u32 GetCharacteristics(const std::vector<u8>& input, std::vector<u8>& output);
    u32 GetTPCMasks(const std::vector<u8>& input, std::vector<u8>& output);
    u32 GetActiveSlotMask(const std::vector<u8>& input, std::vector<u8>& output);
    u32 ZCullGetCtxSize(const std::vector<u8>& input, std::vector<u8>& output);
    u32 ZCullGetInfo(const std::vector<u8>& input, std::vector<u8>& output);
    u32 ZBCSetTable(const std::vector<u8>& input, std::vector<u8>& output);
    u32 ZBCQueryTable(const std::vector<u8>& input, std::vector<u8>& output);
    u32 FlushL2(const std::vector<u8>& input, std::vector<u8>& output);
    u32 GetGpuTime(const std::vector<u8>& input, std::vector<u8>& output);
};

} // namespace Service::Nvidia::Devices
