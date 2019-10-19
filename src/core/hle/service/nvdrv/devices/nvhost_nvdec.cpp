// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>

#include "common/assert.h"
#include "common/logging/log.h"
#include "core/hle/service/nvdrv/devices/nvhost_nvdec.h"

namespace Service::Nvidia::Devices {

nvhost_nvdec::nvhost_nvdec(Core::System& system) : nvdevice(system) {}
nvhost_nvdec::~nvhost_nvdec() = default;

u32 nvhost_nvdec::ioctl(Ioctl command, const std::vector<u8>& input, const std::vector<u8>& input2,
                        std::vector<u8>& output, std::vector<u8>& output2, IoctlCtrl& ctrl,
                        IoctlVersion version) {
    LOG_DEBUG(Service_NVDRV, "called, command=0x{:08X}, input_size=0x{:X}, output_size=0x{:X}",
              command.raw, input.size(), output.size());

    switch (static_cast<IoctlCommand>(command.raw)) {
    case IoctlCommand::IocSetNVMAPfdCommand:
        return SetNVMAPfd(input, output);
    case IoctlCommand::IocSubmit:
        return Submit(input, output);
    case IoctlCommand::IocGetSyncpoint:
        return GetSyncpoint(input, output);
    case IoctlCommand::IocGetWaitbase:
        return GetWaitbase(input, output);
    case IoctlCommand::IocMapBuffer:
        return MapBuffer(input, output);
    case IoctlCommand::IocMapBufferEx:
        return MapBufferEx(input, output);
    case IoctlCommand::IocUnmapBufferEx:
        return UnmapBufferEx(input, output);
    }

    UNIMPLEMENTED_MSG("Unimplemented ioctl");
    return 0;
}

u32 nvhost_nvdec::SetNVMAPfd(const std::vector<u8>& input, std::vector<u8>& output) {
    IoctlSetNvmapFD params{};
    std::memcpy(&params, input.data(), sizeof(IoctlSetNvmapFD));
    LOG_DEBUG(Service_NVDRV, "called, fd={}", params.nvmap_fd);

    nvmap_fd = params.nvmap_fd;
    return 0;
}

u32 nvhost_nvdec::Submit(const std::vector<u8>& input, std::vector<u8>& output) {
    IoctlSubmit params{};
    std::memcpy(&params, input.data(), sizeof(IoctlSubmit));
    LOG_WARNING(Service_NVDRV, "(STUBBED) called");
    std::memcpy(output.data(), &params, sizeof(IoctlSubmit));
    return 0;
}

u32 nvhost_nvdec::GetSyncpoint(const std::vector<u8>& input, std::vector<u8>& output) {
    IoctlGetSyncpoint params{};
    std::memcpy(&params, input.data(), sizeof(IoctlGetSyncpoint));
    LOG_INFO(Service_NVDRV, "called, unknown=0x{:X}", params.unknown);
    params.value = 0; // Seems to be hard coded at 0
    std::memcpy(output.data(), &params, sizeof(IoctlGetSyncpoint));
    return 0;
}

u32 nvhost_nvdec::GetWaitbase(const std::vector<u8>& input, std::vector<u8>& output) {
    IoctlGetWaitbase params{};
    std::memcpy(&params, input.data(), sizeof(IoctlGetWaitbase));
    LOG_INFO(Service_NVDRV, "called, unknown=0x{:X}", params.unknown);
    params.value = 0; // Seems to be hard coded at 0
    std::memcpy(output.data(), &params, sizeof(IoctlGetWaitbase));
    return 0;
}

u32 nvhost_nvdec::MapBuffer(const std::vector<u8>& input, std::vector<u8>& output) {
    IoctlMapBuffer params{};
    std::memcpy(&params, input.data(), sizeof(IoctlMapBuffer));
    LOG_WARNING(Service_NVDRV, "(STUBBED) called with address={:08X}{:08X}", params.address_2,
                params.address_1);
    params.address_1 = 0;
    params.address_2 = 0;
    std::memcpy(output.data(), &params, sizeof(IoctlMapBuffer));
    return 0;
}

u32 nvhost_nvdec::MapBufferEx(const std::vector<u8>& input, std::vector<u8>& output) {
    IoctlMapBufferEx params{};
    std::memcpy(&params, input.data(), sizeof(IoctlMapBufferEx));
    LOG_WARNING(Service_NVDRV, "(STUBBED) called with address={:08X}{:08X}", params.address_2,
                params.address_1);
    params.address_1 = 0;
    params.address_2 = 0;
    std::memcpy(output.data(), &params, sizeof(IoctlMapBufferEx));
    return 0;
}

u32 nvhost_nvdec::UnmapBufferEx(const std::vector<u8>& input, std::vector<u8>& output) {
    IoctlUnmapBufferEx params{};
    std::memcpy(&params, input.data(), sizeof(IoctlUnmapBufferEx));
    LOG_WARNING(Service_NVDRV, "(STUBBED) called");
    std::memcpy(output.data(), &params, sizeof(IoctlUnmapBufferEx));
    return 0;
}

} // namespace Service::Nvidia::Devices
