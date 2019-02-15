// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <fmt/format.h>
#include "common/assert.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/hle/ipc.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/client_port.h"
#include "core/hle/kernel/handle_table.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/server_port.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/service/acc/acc.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/aoc/aoc_u.h"
#include "core/hle/service/apm/apm.h"
#include "core/hle/service/arp/arp.h"
#include "core/hle/service/audio/audio.h"
#include "core/hle/service/bcat/module.h"
#include "core/hle/service/bpc/bpc.h"
#include "core/hle/service/btdrv/btdrv.h"
#include "core/hle/service/btm/btm.h"
#include "core/hle/service/caps/caps.h"
#include "core/hle/service/erpt/erpt.h"
#include "core/hle/service/es/es.h"
#include "core/hle/service/eupld/eupld.h"
#include "core/hle/service/fatal/fatal.h"
#include "core/hle/service/fgm/fgm.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/friend/friend.h"
#include "core/hle/service/grc/grc.h"
#include "core/hle/service/hid/hid.h"
#include "core/hle/service/lbl/lbl.h"
#include "core/hle/service/ldn/ldn.h"
#include "core/hle/service/ldr/ldr.h"
#include "core/hle/service/lm/lm.h"
#include "core/hle/service/mig/mig.h"
#include "core/hle/service/mii/mii.h"
#include "core/hle/service/mm/mm_u.h"
#include "core/hle/service/ncm/ncm.h"
#include "core/hle/service/nfc/nfc.h"
#include "core/hle/service/nfp/nfp.h"
#include "core/hle/service/nifm/nifm.h"
#include "core/hle/service/nim/nim.h"
#include "core/hle/service/npns/npns.h"
#include "core/hle/service/ns/ns.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "core/hle/service/nvflinger/nvflinger.h"
#include "core/hle/service/pcie/pcie.h"
#include "core/hle/service/pctl/module.h"
#include "core/hle/service/pcv/pcv.h"
#include "core/hle/service/pm/pm.h"
#include "core/hle/service/prepo/prepo.h"
#include "core/hle/service/psc/psc.h"
#include "core/hle/service/ptm/psm.h"
#include "core/hle/service/service.h"
#include "core/hle/service/set/settings.h"
#include "core/hle/service/sm/sm.h"
#include "core/hle/service/sockets/sockets.h"
#include "core/hle/service/spl/module.h"
#include "core/hle/service/ssl/ssl.h"
#include "core/hle/service/time/time.h"
#include "core/hle/service/usb/usb.h"
#include "core/hle/service/vi/vi.h"
#include "core/hle/service/wlan/wlan.h"

namespace Service {

/**
 * Creates a function string for logging, complete with the name (or header code, depending
 * on what's passed in) the port name, and all the cmd_buff arguments.
 */
[[maybe_unused]] static std::string MakeFunctionString(const char* name, const char* port_name,
                                                       const u32* cmd_buff) {
    // Number of params == bits 0-5 + bits 6-11
    int num_params = (cmd_buff[0] & 0x3F) + ((cmd_buff[0] >> 6) & 0x3F);

    std::string function_string = fmt::format("function '{}': port={}", name, port_name);
    for (int i = 1; i <= num_params; ++i) {
        function_string += fmt::format(", cmd_buff[{}]=0x{:X}", i, cmd_buff[i]);
    }
    return function_string;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ServiceFrameworkBase::ServiceFrameworkBase(const char* service_name, u32 max_sessions,
                                           InvokerFn* handler_invoker)
    : service_name(service_name), max_sessions(max_sessions), handler_invoker(handler_invoker) {}

ServiceFrameworkBase::~ServiceFrameworkBase() = default;

void ServiceFrameworkBase::InstallAsService(SM::ServiceManager& service_manager) {
    ASSERT(!port_installed);

    auto port = service_manager.RegisterService(service_name, max_sessions).Unwrap();
    port->SetHleHandler(shared_from_this());
    port_installed = true;
}

void ServiceFrameworkBase::InstallAsNamedPort() {
    ASSERT(!port_installed);

    auto& kernel = Core::System::GetInstance().Kernel();
    auto [server_port, client_port] =
        Kernel::ServerPort::CreatePortPair(kernel, max_sessions, service_name);
    server_port->SetHleHandler(shared_from_this());
    kernel.AddNamedPort(service_name, std::move(client_port));
    port_installed = true;
}

Kernel::SharedPtr<Kernel::ClientPort> ServiceFrameworkBase::CreatePort() {
    ASSERT(!port_installed);

    auto& kernel = Core::System::GetInstance().Kernel();
    auto [server_port, client_port] =
        Kernel::ServerPort::CreatePortPair(kernel, max_sessions, service_name);
    auto port = MakeResult(std::move(server_port)).Unwrap();
    port->SetHleHandler(shared_from_this());
    port_installed = true;
    return client_port;
}

void ServiceFrameworkBase::RegisterHandlersBase(const FunctionInfoBase* functions, std::size_t n) {
    handlers.reserve(handlers.size() + n);
    for (std::size_t i = 0; i < n; ++i) {
        // Usually this array is sorted by id already, so hint to insert at the end
        handlers.emplace_hint(handlers.cend(), functions[i].expected_header, functions[i]);
    }
}

void ServiceFrameworkBase::ReportUnimplementedFunction(Kernel::HLERequestContext& ctx,
                                                       const FunctionInfoBase* info) {
    auto cmd_buf = ctx.CommandBuffer();
    std::string function_name = info == nullptr ? fmt::format("{}", ctx.GetCommand()) : info->name;

    fmt::memory_buffer buf;
    fmt::format_to(buf, "function '{}': port='{}' cmd_buf={{[0]=0x{:X}", function_name,
                   service_name, cmd_buf[0]);
    for (int i = 1; i <= 8; ++i) {
        fmt::format_to(buf, ", [{}]=0x{:X}", i, cmd_buf[i]);
    }
    buf.push_back('}');

    UNIMPLEMENTED_MSG("Unknown / unimplemented {}", fmt::to_string(buf));
}

void ServiceFrameworkBase::InvokeRequest(Kernel::HLERequestContext& ctx) {
    auto itr = handlers.find(ctx.GetCommand());
    const FunctionInfoBase* info = itr == handlers.end() ? nullptr : &itr->second;
    if (info == nullptr || info->handler_callback == nullptr) {
        return ReportUnimplementedFunction(ctx, info);
    }

    LOG_TRACE(
        Service, "{}",
        MakeFunctionString(info->name, GetServiceName().c_str(), ctx.CommandBuffer()).c_str());
    handler_invoker(this, info->handler_callback, ctx);
}

ResultCode ServiceFrameworkBase::HandleSyncRequest(Kernel::HLERequestContext& context) {
    switch (context.GetCommandType()) {
    case IPC::CommandType::Close: {
        IPC::ResponseBuilder rb{context, 2};
        rb.Push(RESULT_SUCCESS);
        return ResultCode(ErrorModule::HIPC, ErrorDescription::RemoteProcessDead);
    }
    case IPC::CommandType::ControlWithContext:
    case IPC::CommandType::Control: {
        Core::System::GetInstance().ServiceManager().InvokeControlRequest(context);
        break;
    }
    case IPC::CommandType::RequestWithContext:
    case IPC::CommandType::Request: {
        InvokeRequest(context);
        break;
    }
    default:
        UNIMPLEMENTED_MSG("command_type={}", static_cast<int>(context.GetCommandType()));
    }

    context.WriteToOutgoingCommandBuffer(*Kernel::GetCurrentThread());

    return RESULT_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Module interface

/// Initialize ServiceManager
void Init(std::shared_ptr<SM::ServiceManager>& sm, Core::System& system,
          FileSys::VfsFilesystem& vfs) {
    // NVFlinger needs to be accessed by several services like Vi and AppletOE so we instantiate it
    // here and pass it into the respective InstallInterfaces functions.
    auto nv_flinger = std::make_shared<NVFlinger::NVFlinger>(system.CoreTiming());

    SM::ServiceManager::InstallInterfaces(sm);

    Account::InstallInterfaces(*sm);
    AM::InstallInterfaces(*sm, nv_flinger);
    AOC::InstallInterfaces(*sm);
    APM::InstallInterfaces(*sm);
    ARP::InstallInterfaces(*sm);
    Audio::InstallInterfaces(*sm);
    BCAT::InstallInterfaces(*sm);
    BPC::InstallInterfaces(*sm);
    BtDrv::InstallInterfaces(*sm);
    BTM::InstallInterfaces(*sm);
    Capture::InstallInterfaces(*sm);
    ERPT::InstallInterfaces(*sm);
    ES::InstallInterfaces(*sm);
    EUPLD::InstallInterfaces(*sm);
    Fatal::InstallInterfaces(*sm);
    FGM::InstallInterfaces(*sm);
    FileSystem::InstallInterfaces(*sm, vfs);
    Friend::InstallInterfaces(*sm);
    GRC::InstallInterfaces(*sm);
    HID::InstallInterfaces(*sm);
    LBL::InstallInterfaces(*sm);
    LDN::InstallInterfaces(*sm);
    LDR::InstallInterfaces(*sm);
    LM::InstallInterfaces(*sm);
    Migration::InstallInterfaces(*sm);
    Mii::InstallInterfaces(*sm);
    MM::InstallInterfaces(*sm);
    NCM::InstallInterfaces(*sm);
    NFC::InstallInterfaces(*sm);
    NFP::InstallInterfaces(*sm);
    NIFM::InstallInterfaces(*sm);
    NIM::InstallInterfaces(*sm);
    NPNS::InstallInterfaces(*sm);
    NS::InstallInterfaces(*sm);
    Nvidia::InstallInterfaces(*sm, *nv_flinger);
    PCIe::InstallInterfaces(*sm);
    PCTL::InstallInterfaces(*sm);
    PCV::InstallInterfaces(*sm);
    PlayReport::InstallInterfaces(*sm);
    PM::InstallInterfaces(*sm);
    PSC::InstallInterfaces(*sm);
    PSM::InstallInterfaces(*sm);
    Set::InstallInterfaces(*sm);
    Sockets::InstallInterfaces(*sm);
    SPL::InstallInterfaces(*sm);
    SSL::InstallInterfaces(*sm);
    Time::InstallInterfaces(*sm);
    USB::InstallInterfaces(*sm);
    VI::InstallInterfaces(*sm, nv_flinger);
    WLAN::InstallInterfaces(*sm);

    LOG_DEBUG(Service, "initialized OK");
}

/// Shutdown ServiceManager
void Shutdown() {
    LOG_DEBUG(Service, "shutdown OK");
}
} // namespace Service
