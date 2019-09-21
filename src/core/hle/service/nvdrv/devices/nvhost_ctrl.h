// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <vector>
#include "common/common_types.h"
#include "core/hle/service/nvdrv/devices/nvdevice.h"
#include "core/hle/service/nvdrv/nvdrv.h"

namespace Service::Nvidia::Devices {

class nvhost_ctrl final : public nvdevice {
public:
    explicit nvhost_ctrl(Core::System& system, EventInterface& events_interface);
    ~nvhost_ctrl() override;

    u32 ioctl(Ioctl command, const std::vector<u8>& input, const std::vector<u8>& input2,
              std::vector<u8>& output, std::vector<u8>& output2, IoctlCtrl& ctrl,
              IoctlVersion version) override;

private:
    enum class IoctlCommand : u32_le {
        IocSyncptReadCommand = 0xC0080014,
        IocSyncptIncrCommand = 0x40040015,
        IocSyncptWaitCommand = 0xC00C0016,
        IocModuleMutexCommand = 0x40080017,
        IocModuleRegRDWRCommand = 0xC0180018,
        IocSyncptWaitexCommand = 0xC0100019,
        IocSyncptReadMaxCommand = 0xC008001A,
        IocGetConfigCommand = 0xC183001B,
        IocCtrlEventSignalCommand = 0xC004001C,
        IocCtrlEventWaitCommand = 0xC010001D,
        IocCtrlEventWaitAsyncCommand = 0xC010001E,
        IocCtrlEventRegisterCommand = 0xC004001F,
        IocCtrlEventUnregisterCommand = 0xC0040020,
        IocCtrlEventKillCommand = 0x40080021,
    };
    struct IocSyncptReadParams {
        u32_le id;
        u32_le value;
    };
    static_assert(sizeof(IocSyncptReadParams) == 8, "IocSyncptReadParams is incorrect size");

    struct IocSyncptIncrParams {
        u32_le id;
    };
    static_assert(sizeof(IocSyncptIncrParams) == 4, "IocSyncptIncrParams is incorrect size");

    struct IocSyncptWaitParams {
        u32_le id;
        u32_le thresh;
        s32_le timeout;
    };
    static_assert(sizeof(IocSyncptWaitParams) == 12, "IocSyncptWaitParams is incorrect size");

    struct IocModuleMutexParams {
        u32_le id;
        u32_le lock; // (0 = unlock and 1 = lock)
    };
    static_assert(sizeof(IocModuleMutexParams) == 8, "IocModuleMutexParams is incorrect size");

    struct IocModuleRegRDWRParams {
        u32_le id;
        u32_le num_offsets;
        u32_le block_size;
        u32_le offsets;
        u32_le values;
        u32_le write;
    };
    static_assert(sizeof(IocModuleRegRDWRParams) == 24, "IocModuleRegRDWRParams is incorrect size");

    struct IocSyncptWaitexParams {
        u32_le id;
        u32_le thresh;
        s32_le timeout;
        u32_le value;
    };
    static_assert(sizeof(IocSyncptWaitexParams) == 16, "IocSyncptWaitexParams is incorrect size");

    struct IocSyncptReadMaxParams {
        u32_le id;
        u32_le value;
    };
    static_assert(sizeof(IocSyncptReadMaxParams) == 8, "IocSyncptReadMaxParams is incorrect size");

    struct IocGetConfigParams {
        std::array<char, 0x41> domain_str;
        std::array<char, 0x41> param_str;
        std::array<char, 0x101> config_str;
    };
    static_assert(sizeof(IocGetConfigParams) == 387, "IocGetConfigParams is incorrect size");

    struct IocCtrlEventSignalParams {
        u32_le user_event_id;
    };
    static_assert(sizeof(IocCtrlEventSignalParams) == 4,
                  "IocCtrlEventSignalParams is incorrect size");

    struct IocCtrlEventWaitParams {
        u32_le syncpt_id;
        u32_le threshold;
        s32_le timeout;
        u32_le value;
    };
    static_assert(sizeof(IocCtrlEventWaitParams) == 16, "IocCtrlEventWaitParams is incorrect size");

    struct IocCtrlEventWaitAsyncParams {
        u32_le syncpt_id;
        u32_le threshold;
        u32_le timeout;
        u32_le value;
    };
    static_assert(sizeof(IocCtrlEventWaitAsyncParams) == 16,
                  "IocCtrlEventWaitAsyncParams is incorrect size");

    struct IocCtrlEventRegisterParams {
        u32_le user_event_id;
    };
    static_assert(sizeof(IocCtrlEventRegisterParams) == 4,
                  "IocCtrlEventRegisterParams is incorrect size");

    struct IocCtrlEventUnregisterParams {
        u32_le user_event_id;
    };
    static_assert(sizeof(IocCtrlEventUnregisterParams) == 4,
                  "IocCtrlEventUnregisterParams is incorrect size");

    struct IocCtrlEventKill {
        u64_le user_events;
    };
    static_assert(sizeof(IocCtrlEventKill) == 8, "IocCtrlEventKill is incorrect size");

    u32 NvOsGetConfigU32(const std::vector<u8>& input, std::vector<u8>& output);

    u32 IocCtrlEventWait(const std::vector<u8>& input, std::vector<u8>& output, bool is_async,
                         IoctlCtrl& ctrl);

    u32 IocCtrlEventRegister(const std::vector<u8>& input, std::vector<u8>& output);

    u32 IocCtrlEventUnregister(const std::vector<u8>& input, std::vector<u8>& output);

    u32 IocCtrlEventSignal(const std::vector<u8>& input, std::vector<u8>& output);

    EventInterface& events_interface;
};

} // namespace Service::Nvidia::Devices
