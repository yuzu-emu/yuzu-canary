// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <sstream>
#include <string>

#include "common/logging/log.h"
#include "common/scope_exit.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/service/lm/lm.h"
#include "core/hle/service/lm/manager.h"
#include "core/hle/service/service.h"
#include "core/memory.h"

namespace Service::LM {

class ILogger final : public ServiceFramework<ILogger> {
public:
    ILogger(Manager& manager) : ServiceFramework("ILogger"), manager(manager) {
        static const FunctionInfo functions[] = {
            {0, &ILogger::Log, "Log"},
            {1, &ILogger::SetDestination, "SetDestination"},
        };
        RegisterHandlers(functions);
    }

private:
    void Log(Kernel::HLERequestContext& ctx) {
        // This function only succeeds - Get that out of the way
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);

        // Read MessageHeader, despite not doing anything with it right now
        MessageHeader header{};
        VAddr addr{ctx.BufferDescriptorX()[0].Address()};
        const VAddr end_addr{addr + ctx.BufferDescriptorX()[0].size};
        Memory::ReadBlock(addr, &header, sizeof(MessageHeader));
        addr += sizeof(MessageHeader);

        FieldMap fields;
        while (addr < end_addr) {
            const auto field = static_cast<Field>(Memory::Read8(addr++));
            const auto length = Memory::Read8(addr++);

            if (static_cast<Field>(Memory::Read8(addr)) == Field::Skip) {
                ++addr;
            }

            SCOPE_EXIT({ addr += length; });

            if (field == Field::Skip) {
                continue;
            }

            std::vector<u8> data(length);
            Memory::ReadBlock(addr, data.data(), length);
            fields.emplace(field, std::move(data));
        }

        manager.Log({header, std::move(fields)});
    }

    void SetDestination(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto destination = rp.PopEnum<DestinationFlag>();

        LOG_DEBUG(Service_LM, "called, destination={:08X}", static_cast<u32>(destination));

        manager.SetDestination(destination);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    Manager& manager;
};

class LM final : public ServiceFramework<LM> {
public:
    explicit LM(Manager& manager) : ServiceFramework{"lm"}, manager(manager) {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &LM::OpenLogger, "OpenLogger"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void OpenLogger(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_LM, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<ILogger>(manager);
    }

    Manager& manager;
};

void InstallInterfaces(Core::System& system) {
    std::make_shared<LM>(system.GetLogManager())->InstallAsService(system.ServiceManager());
}

} // namespace Service::LM
