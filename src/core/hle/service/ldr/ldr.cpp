// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <fmt/format.h>

#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/ldr/ldr.h"
#include "core/hle/service/service.h"
#include "core/loader/nro.h"

namespace Service::LDR {

class DebugMonitor final : public ServiceFramework<DebugMonitor> {
public:
    explicit DebugMonitor() : ServiceFramework{"ldr:dmnt"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "AddProcessToDebugLaunchQueue"},
            {1, nullptr, "ClearDebugLaunchQueue"},
            {2, nullptr, "GetNsoInfos"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class ProcessManager final : public ServiceFramework<ProcessManager> {
public:
    explicit ProcessManager() : ServiceFramework{"ldr:pm"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "CreateProcess"},
            {1, nullptr, "GetProgramInfo"},
            {2, nullptr, "RegisterTitle"},
            {3, nullptr, "UnregisterTitle"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class Shell final : public ServiceFramework<Shell> {
public:
    explicit Shell() : ServiceFramework{"ldr:shel"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "AddProcessToLaunchQueue"},
            {1, nullptr, "ClearLaunchQueue"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class RelocatableObject final : public ServiceFramework<RelocatableObject> {
public:
    explicit RelocatableObject() : ServiceFramework{"ldr:ro"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &RelocatableObject::LoadNro, "LoadNro"},
            {1, nullptr, "UnloadNro"},
            {2, &RelocatableObject::LoadNrr, "LoadNrr"},
            {3, nullptr, "UnloadNrr"},
            {4, &RelocatableObject::Initialize, "Initialize"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void LoadNrr(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        LOG_WARNING(Service_LDR, "(STUBBED) called");
    }

    void LoadNro(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        rp.Skip(2, false);
        const VAddr nro_addr{rp.Pop<VAddr>()};
        const u64 nro_size{rp.Pop<u64>()};
        const VAddr bss_addr{rp.Pop<VAddr>()};
        const u64 bss_size{rp.Pop<u64>()};

        // Read NRO data from memory
        std::vector<u8> nro_data(nro_size);
        Memory::ReadBlock(nro_addr, nro_data.data(), nro_size);

        // Load NRO as new executable module
        const VAddr addr{*Core::CurrentProcess()->VMManager().FindFreeRegion(nro_size + bss_size)};
        Loader::AppLoader_NRO::LoadNro(nro_data, fmt::format("nro-{:08x}", addr), addr);

        // TODO(bunnei): This is an incomplete implementation. It was tested with Super Mario Party.
        // It is currently missing:
        // - Signature checks with LoadNRR
        // - Checking if a module has already been loaded
        // - Using/validating BSS, etc. params (these are used from NRO header instead)
        // - Error checking
        // - ...Probably other things

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(RESULT_SUCCESS);
        rb.Push(addr);
        LOG_WARNING(Service_LDR, "(STUBBED) called");
    }

    void Initialize(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        LOG_WARNING(Service_LDR, "(STUBBED) called");
    }
};

void InstallInterfaces(SM::ServiceManager& sm) {
    std::make_shared<DebugMonitor>()->InstallAsService(sm);
    std::make_shared<ProcessManager>()->InstallAsService(sm);
    std::make_shared<Shell>()->InstallAsService(sm);
    std::make_shared<RelocatableObject>()->InstallAsService(sm);
}

} // namespace Service::LDR
