// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <vector>
#include "common/common_types.h"
#include "common/swap.h"
#include "core/hle/service/nvdrv/devices/nvdevice.h"

namespace Service::Nvidia::Devices {

class nvhost_nvdec final : public nvdevice {
public:
    explicit nvhost_nvdec(Core::System& system);
    ~nvhost_nvdec() override;

    u32 ioctl(Ioctl command, const std::vector<u8>& input, const std::vector<u8>& input2,
              std::vector<u8>& output, std::vector<u8>& output2, IoctlCtrl& ctrl,
              IoctlVersion version) override;

private:
    enum class IoctlCommand : u32_le {
        IocSetNVMAPfdCommand = 0x40044801,
        IocSubmit = 0xC0400001,
        IocGetSyncpoint = 0xC0080002,
        IocGetWaitbase = 0xC0080003,
        IocMapBuffer = 0xC01C0009,
        IocMapBufferEx = 0xC0A40009,
        IocUnmapBufferEx = 0xC0A4000A,
    };

    struct IoctlSetNvmapFD {
        u32_le nvmap_fd;
    };
    static_assert(sizeof(IoctlSetNvmapFD) == 0x4, "IoctlSetNvmapFD is incorrect size");

    struct IoctlSubmit {
        INSERT_PADDING_BYTES(0x40); // TODO(DarkLordZach): RE this structure
    };
    static_assert(sizeof(IoctlSubmit) == 0x40, "IoctlSubmit has incorrect size");

    struct IoctlGetSyncpoint {
        u32 unknown; // seems to be ignored? Nintendo added this
        u32 value;
    };
    static_assert(sizeof(IoctlGetSyncpoint) == 0x08, "IoctlGetSyncpoint has incorrect size");

    struct IoctlGetWaitbase {
        u32 unknown; // seems to be ignored? Nintendo added this
        u32 value;
    };
    static_assert(sizeof(IoctlGetWaitbase) == 0x08, "IoctlGetWaitbase has incorrect size");

    struct IoctlMapBuffer {
        u32 unknown;
        u32 address_1;
        u32 address_2;
        INSERT_PADDING_BYTES(0x10); // TODO(DarkLordZach): RE this structure
    };
    static_assert(sizeof(IoctlMapBuffer) == 0x1C, "IoctlMapBuffer is incorrect size");

    struct IoctlMapBufferEx {
        u32 unknown;
        u32 address_1;
        u32 address_2;
        INSERT_PADDING_BYTES(0x98); // TODO(DarkLordZach): RE this structure
    };
    static_assert(sizeof(IoctlMapBufferEx) == 0xA4, "IoctlMapBufferEx has incorrect size");

    struct IoctlUnmapBufferEx {
        INSERT_PADDING_BYTES(0xA4); // TODO(DarkLordZach): RE this structure
    };
    static_assert(sizeof(IoctlUnmapBufferEx) == 0xA4, "IoctlUnmapBufferEx has incorrect size");

    u32_le nvmap_fd{};

    u32 SetNVMAPfd(const std::vector<u8>& input, std::vector<u8>& output);
    u32 Submit(const std::vector<u8>& input, std::vector<u8>& output);
    u32 GetSyncpoint(const std::vector<u8>& input, std::vector<u8>& output);
    u32 GetWaitbase(const std::vector<u8>& input, std::vector<u8>& output);
    u32 MapBuffer(const std::vector<u8>& input, std::vector<u8>& output);
    u32 MapBufferEx(const std::vector<u8>& input, std::vector<u8>& output);
    u32 UnmapBufferEx(const std::vector<u8>& input, std::vector<u8>& output);
};

} // namespace Service::Nvidia::Devices
