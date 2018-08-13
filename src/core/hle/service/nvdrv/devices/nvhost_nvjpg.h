// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <vector>
#include "common/common_types.h"
#include "common/swap.h"
#include "core/hle/service/nvdrv/devices/nvdevice.h"

namespace Service::Nvidia::Devices {

class nvhost_nvjpg final : public nvdevice {
public:
    nvhost_nvjpg() = default;
    ~nvhost_nvjpg() override = default;

    u32 ioctl(Ioctl command, const std::vector<u8>& input, std::vector<u8>& output) override;

private:
    enum class IoctlCommand : u32_le {
        IocSetNVMAPfdCommand = 0x40044801,
    };

    struct IoctlSetNvmapFD {
        u32_le nvmap_fd;
    };
    static_assert(sizeof(IoctlSetNvmapFD) == 4, "IoctlSetNvmapFD is incorrect size");

    u32_le nvmap_fd{};

    u32 SetNVMAPfd(const std::vector<u8>& input, std::vector<u8>& output);
};

} // namespace Service::Nvidia::Devices
