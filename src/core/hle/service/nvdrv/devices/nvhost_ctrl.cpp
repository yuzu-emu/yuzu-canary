// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstdlib>
#include <cstring>

#include "common/assert.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/kernel/readable_event.h"
#include "core/hle/kernel/writable_event.h"
#include "core/hle/service/nvdrv/devices/nvhost_ctrl.h"
#include "video_core/gpu.h"

namespace Service::Nvidia::Devices {

nvhost_ctrl::nvhost_ctrl(Core::System& system, EventInterface& events_interface)
    : nvdevice(system), events_interface{events_interface} {}
nvhost_ctrl::~nvhost_ctrl() = default;

u32 nvhost_ctrl::ioctl(Ioctl command, const std::vector<u8>& input, const std::vector<u8>& input2,
                       std::vector<u8>& output, std::vector<u8>& output2, IoctlCtrl& ctrl,
                       IoctlVersion version) {
    LOG_DEBUG(Service_NVDRV, "called, command=0x{:08X}, input_size=0x{:X}, output_size=0x{:X}",
              command.raw, input.size(), output.size());

    switch (static_cast<IoctlCommand>(command.raw)) {
    case IoctlCommand::IocGetConfigCommand:
        return NvOsGetConfigU32(input, output);
    case IoctlCommand::IocCtrlEventWaitCommand:
        return IocCtrlEventWait(input, output, false, ctrl);
    case IoctlCommand::IocCtrlEventWaitAsyncCommand:
        return IocCtrlEventWait(input, output, true, ctrl);
    case IoctlCommand::IocCtrlEventRegisterCommand:
        return IocCtrlEventRegister(input, output);
    case IoctlCommand::IocCtrlEventUnregisterCommand:
        return IocCtrlEventUnregister(input, output);
    case IoctlCommand::IocCtrlEventSignalCommand:
        return IocCtrlEventSignal(input, output);
    default:
        UNIMPLEMENTED_MSG("Unimplemented ioctl");
        return 0;
    }
}

u32 nvhost_ctrl::NvOsGetConfigU32(const std::vector<u8>& input, std::vector<u8>& output) {
    IocGetConfigParams params{};
    std::memcpy(&params, input.data(), sizeof(params));
    LOG_TRACE(Service_NVDRV, "called, setting={}!{}", params.domain_str.data(),
              params.param_str.data());
    return 0x30006; // Returns error on production mode
}

u32 nvhost_ctrl::IocCtrlEventWait(const std::vector<u8>& input, std::vector<u8>& output,
                                  bool is_async, IoctlCtrl& ctrl) {
    IocCtrlEventWaitParams params{};
    std::memcpy(&params, input.data(), sizeof(params));
    LOG_DEBUG(Service_NVDRV, "syncpt_id={}, threshold={}, timeout={}, is_async={}",
              params.syncpt_id, params.threshold, params.timeout, is_async);

    if (params.syncpt_id >= MaxSyncPoints) {
        return NvResult::BadParameter;
    }

    u32 event_id = params.value & 0x00FF;

    if (event_id >= MaxNvEvents) {
        std::memcpy(output.data(), &params, sizeof(params));
        return NvResult::BadParameter;
    }

    auto event = events_interface.events[event_id];
    auto& gpu = system.GPU();
    // This is mostly to take into account unimplemented features. As synced
    // gpu is always synced.
    if (!gpu.IsAsync()) {
        event.writable->Signal();
        return NvResult::Success;
    }
    auto lock = gpu.LockSync();
    const u32 current_syncpoint_value = gpu.GetSyncpointValue(params.syncpt_id);
    const s32 diff = current_syncpoint_value - params.threshold;
    if (diff >= 0) {
        event.writable->Signal();
        params.value = current_syncpoint_value;
        std::memcpy(output.data(), &params, sizeof(params));
        return NvResult::Success;
    }
    const u32 target_value = current_syncpoint_value - diff;

    if (!is_async) {
        params.value = 0;
    }

    if (params.timeout == 0) {
        std::memcpy(output.data(), &params, sizeof(params));
        return NvResult::Timeout;
    }

    EventState status = events_interface.status[event_id];
    if (event_id < MaxNvEvents || status == EventState::Free || status == EventState::Registered) {
        events_interface.SetEventStatus(event_id, EventState::Waiting);
        events_interface.assigned_syncpt[event_id] = params.syncpt_id;
        events_interface.assigned_value[event_id] = target_value;
        if (is_async) {
            params.value = params.syncpt_id << 4;
        } else {
            params.value = ((params.syncpt_id & 0xfff) << 16) | 0x10000000;
        }
        params.value |= event_id;
        event.writable->Clear();
        gpu.RegisterSyncptInterrupt(params.syncpt_id, target_value);
        if (!is_async && ctrl.fresh_call) {
            ctrl.must_delay = true;
            ctrl.timeout = params.timeout;
            ctrl.event_id = event_id;
            return NvResult::Timeout;
        }
        std::memcpy(output.data(), &params, sizeof(params));
        return NvResult::Timeout;
    }
    std::memcpy(output.data(), &params, sizeof(params));
    return NvResult::BadParameter;
}

u32 nvhost_ctrl::IocCtrlEventRegister(const std::vector<u8>& input, std::vector<u8>& output) {
    IocCtrlEventRegisterParams params{};
    std::memcpy(&params, input.data(), sizeof(params));
    const u32 event_id = params.user_event_id & 0x00FF;
    LOG_DEBUG(Service_NVDRV, " called, user_event_id: {:X}", event_id);
    if (event_id >= MaxNvEvents) {
        return NvResult::BadParameter;
    }
    if (events_interface.registered[event_id]) {
        return NvResult::BadParameter;
    }
    events_interface.RegisterEvent(event_id);
    return NvResult::Success;
}

u32 nvhost_ctrl::IocCtrlEventUnregister(const std::vector<u8>& input, std::vector<u8>& output) {
    IocCtrlEventUnregisterParams params{};
    std::memcpy(&params, input.data(), sizeof(params));
    const u32 event_id = params.user_event_id & 0x00FF;
    LOG_DEBUG(Service_NVDRV, " called, user_event_id: {:X}", event_id);
    if (event_id >= MaxNvEvents) {
        return NvResult::BadParameter;
    }
    if (!events_interface.registered[event_id]) {
        return NvResult::BadParameter;
    }
    events_interface.UnregisterEvent(event_id);
    return NvResult::Success;
}

u32 nvhost_ctrl::IocCtrlEventSignal(const std::vector<u8>& input, std::vector<u8>& output) {
    IocCtrlEventSignalParams params{};
    std::memcpy(&params, input.data(), sizeof(params));
    // TODO(Blinkhawk): This is normally called when an NvEvents timeout on WaitSynchronization
    // It is believed from RE to cancel the GPU Event. However, better research is required
    u32 event_id = params.user_event_id & 0x00FF;
    LOG_WARNING(Service_NVDRV, "(STUBBED) called, user_event_id: {:X}", event_id);
    if (event_id >= MaxNvEvents) {
        return NvResult::BadParameter;
    }
    if (events_interface.status[event_id] == EventState::Waiting) {
        auto& gpu = system.GPU();
        if (gpu.CancelSyncptInterrupt(events_interface.assigned_syncpt[event_id],
                                      events_interface.assigned_value[event_id])) {
            events_interface.LiberateEvent(event_id);
            events_interface.events[event_id].writable->Signal();
        }
    }
    return NvResult::Success;
}

} // namespace Service::Nvidia::Devices
