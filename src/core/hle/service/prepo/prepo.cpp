// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/hex_util.h"
#include "common/logging/log.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/hle/service/prepo/prepo.h"
#include "core/hle/service/service.h"
#include "core/reporter.h"

namespace Service::PlayReport {

class PlayReport final : public ServiceFramework<PlayReport> {
public:
    explicit PlayReport(Core::System& system, const char* name)
        : ServiceFramework{name}, system(system) {
        // clang-format off
        static const FunctionInfo functions[] = {
            {10100, &PlayReport::SaveReport<Core::Reporter::PlayReportType::Old>, "SaveReportOld"},
            {10101, &PlayReport::SaveReportWithUser<Core::Reporter::PlayReportType::Old>, "SaveReportWithUserOld"},
            {10102, &PlayReport::SaveReport<Core::Reporter::PlayReportType::New>, "SaveReport"},
            {10103, &PlayReport::SaveReportWithUser<Core::Reporter::PlayReportType::New>, "SaveReportWithUser"},
            {10200, nullptr, "RequestImmediateTransmission"},
            {10300, nullptr, "GetTransmissionStatus"},
            {20100, &PlayReport::SaveSystemReport, "SaveSystemReport"},
            {20101, &PlayReport::SaveSystemReportWithUser, "SaveSystemReportWithUser"},
            {20200, nullptr, "SetOperationMode"},
            {30100, nullptr, "ClearStorage"},
            {30200, nullptr, "ClearStatistics"},
            {30300, nullptr, "GetStorageUsage"},
            {30400, nullptr, "GetStatistics"},
            {30401, nullptr, "GetThroughputHistory"},
            {30500, nullptr, "GetLastUploadError"},
            {40100, nullptr, "IsUserAgreementCheckEnabled"},
            {40101, nullptr, "SetUserAgreementCheckEnabled"},
            {90100, nullptr, "ReadAllReportFiles"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    template <Core::Reporter::PlayReportType Type>
    void SaveReport(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto process_id = rp.PopRaw<u64>();

        const auto data1 = ctx.ReadBuffer(0);
        const auto data2 = ctx.ReadBuffer(1);

        LOG_DEBUG(Service_PREPO,
                  "called, type={:02X}, process_id={:016X}, data1_size={:016X}, data2_size={:016X}",
                  static_cast<u8>(Type), process_id, data1.size(), data2.size());

        const auto& reporter{system.GetReporter()};
        reporter.SavePlayReport(Type, system.CurrentProcess()->GetTitleID(), {data1, data2},
                                process_id);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    template <Core::Reporter::PlayReportType Type>
    void SaveReportWithUser(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto user_id = rp.PopRaw<u128>();
        const auto process_id = rp.PopRaw<u64>();

        const auto data1 = ctx.ReadBuffer(0);
        const auto data2 = ctx.ReadBuffer(1);

        LOG_DEBUG(
            Service_PREPO,
            "called, type={:02X}, user_id={:016X}{:016X}, process_id={:016X}, data1_size={:016X}, "
            "data2_size={:016X}",
            static_cast<u8>(Type), user_id[1], user_id[0], process_id, data1.size(), data2.size());

        const auto& reporter{system.GetReporter()};
        reporter.SavePlayReport(Type, system.CurrentProcess()->GetTitleID(), {data1, data2},
                                process_id, user_id);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void SaveSystemReport(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto title_id = rp.PopRaw<u64>();

        const auto data1 = ctx.ReadBuffer(0);
        const auto data2 = ctx.ReadBuffer(1);

        LOG_DEBUG(Service_PREPO, "called, title_id={:016X}, data1_size={:016X}, data2_size={:016X}",
                  title_id, data1.size(), data2.size());

        const auto& reporter{system.GetReporter()};
        reporter.SavePlayReport(Core::Reporter::PlayReportType::System, title_id, {data1, data2});

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void SaveSystemReportWithUser(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto user_id = rp.PopRaw<u128>();
        const auto title_id = rp.PopRaw<u64>();

        const auto data1 = ctx.ReadBuffer(0);
        const auto data2 = ctx.ReadBuffer(1);

        LOG_DEBUG(Service_PREPO,
                  "called, user_id={:016X}{:016X}, title_id={:016X}, data1_size={:016X}, "
                  "data2_size={:016X}",
                  user_id[1], user_id[0], title_id, data1.size(), data2.size());

        const auto& reporter{system.GetReporter()};
        reporter.SavePlayReport(Core::Reporter::PlayReportType::System, title_id, {data1, data2},
                                std::nullopt, user_id);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    Core::System& system;
};

void InstallInterfaces(Core::System& system) {
    std::make_shared<PlayReport>(system, "prepo:a")->InstallAsService(system.ServiceManager());
    std::make_shared<PlayReport>(system, "prepo:a2")->InstallAsService(system.ServiceManager());
    std::make_shared<PlayReport>(system, "prepo:m")->InstallAsService(system.ServiceManager());
    std::make_shared<PlayReport>(system, "prepo:s")->InstallAsService(system.ServiceManager());
    std::make_shared<PlayReport>(system, "prepo:u")->InstallAsService(system.ServiceManager());
}

} // namespace Service::PlayReport
