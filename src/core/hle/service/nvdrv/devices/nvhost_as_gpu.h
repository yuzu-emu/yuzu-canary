// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <unordered_map>
#include <vector>
#include "common/common_types.h"
#include "common/swap.h"
#include "core/hle/service/nvdrv/devices/nvdevice.h"

namespace Service::Nvidia::Devices {

class nvmap;

class nvhost_as_gpu final : public nvdevice {
public:
    explicit nvhost_as_gpu(Core::System& system, std::shared_ptr<nvmap> nvmap_dev);
    ~nvhost_as_gpu() override;

    u32 ioctl(Ioctl command, const std::vector<u8>& input, std::vector<u8>& output,
              IoctlCtrl& ctrl) override;

private:
    enum class IoctlCommand : u32_le {
        IocInitalizeExCommand = 0x40284109,
        IocAllocateSpaceCommand = 0xC0184102,
        IocRemapCommand = 0x00000014,
        IocMapBufferExCommand = 0xC0284106,
        IocBindChannelCommand = 0x40044101,
        IocGetVaRegionsCommand = 0xC0404108,
        IocUnmapBufferCommand = 0xC0084105,
    };

    struct IoctlInitalizeEx {
        u32_le big_page_size; // depends on GPU's available_big_page_sizes; 0=default
        s32_le as_fd;         // ignored; passes 0
        u32_le flags;         // passes 0
        u32_le reserved;      // ignored; passes 0
        u64_le unk0;
        u64_le unk1;
        u64_le unk2;
    };
    static_assert(sizeof(IoctlInitalizeEx) == 40, "IoctlInitalizeEx is incorrect size");

    struct IoctlAllocSpace {
        u32_le pages;
        u32_le page_size;
        u32_le flags;
        INSERT_PADDING_WORDS(1);
        union {
            u64_le offset;
            u64_le align;
        };
    };
    static_assert(sizeof(IoctlAllocSpace) == 24, "IoctlInitalizeEx is incorrect size");

    struct IoctlRemapEntry {
        u16_le flags;
        u16_le kind;
        u32_le nvmap_handle;
        INSERT_PADDING_WORDS(1);
        u32_le offset;
        u32_le pages;
    };
    static_assert(sizeof(IoctlRemapEntry) == 20, "IoctlRemapEntry is incorrect size");

    struct IoctlMapBufferEx {
        u32_le flags; // bit0: fixed_offset, bit2: cacheable
        u32_le kind;  // -1 is default
        u32_le nvmap_handle;
        u32_le page_size; // 0 means don't care
        u64_le buffer_offset;
        u64_le mapping_size;
        u64_le offset;
    };
    static_assert(sizeof(IoctlMapBufferEx) == 40, "IoctlMapBufferEx is incorrect size");

    struct IoctlUnmapBuffer {
        u64_le offset;
    };
    static_assert(sizeof(IoctlUnmapBuffer) == 8, "IoctlUnmapBuffer is incorrect size");

    struct IoctlBindChannel {
        u32_le fd;
    };
    static_assert(sizeof(IoctlBindChannel) == 4, "IoctlBindChannel is incorrect size");

    struct IoctlVaRegion {
        u64_le offset;
        u32_le page_size;
        INSERT_PADDING_WORDS(1);
        u64_le pages;
    };
    static_assert(sizeof(IoctlVaRegion) == 24, "IoctlVaRegion is incorrect size");

    struct IoctlGetVaRegions {
        u64_le buf_addr; // (contained output user ptr on linux, ignored)
        u32_le buf_size; // forced to 2*sizeof(struct va_region)
        u32_le reserved;
        IoctlVaRegion regions[2];
    };
    static_assert(sizeof(IoctlGetVaRegions) == 16 + sizeof(IoctlVaRegion) * 2,
                  "IoctlGetVaRegions is incorrect size");

    struct BufferMapping {
        u64 offset;
        u64 size;
        u32 nvmap_handle;
    };

    /// Map containing the nvmap object mappings in GPU memory.
    std::unordered_map<u64, BufferMapping> buffer_mappings;

    u32 channel{};

    u32 InitalizeEx(const std::vector<u8>& input, std::vector<u8>& output);
    u32 AllocateSpace(const std::vector<u8>& input, std::vector<u8>& output);
    u32 Remap(const std::vector<u8>& input, std::vector<u8>& output);
    u32 MapBufferEx(const std::vector<u8>& input, std::vector<u8>& output);
    u32 UnmapBuffer(const std::vector<u8>& input, std::vector<u8>& output);
    u32 BindChannel(const std::vector<u8>& input, std::vector<u8>& output);
    u32 GetVARegions(const std::vector<u8>& input, std::vector<u8>& output);

    std::shared_ptr<nvmap> nvmap_dev;
};

} // namespace Service::Nvidia::Devices
