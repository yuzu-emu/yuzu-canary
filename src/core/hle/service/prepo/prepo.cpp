// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <json.hpp>
#include "common/file_util.h"
#include "common/hex_util.h"
#include "common/logging/log.h"
#include "common/scm_rev.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/hle/service/prepo/prepo.h"
#include "core/hle/service/service.h"
#include "core/reporter.h"
#include "core/settings.h"

namespace Service::PlayReport {

class PlayReport final : public ServiceFramework<PlayReport> {
public:
    explicit PlayReport(const char* name) : ServiceFramework{name} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {10100, nullptr, "SaveReportOld"},
            {10101, &PlayReport::SaveReportWithUserOld, "SaveReportWithUserOld"},
            {10102, nullptr, "SaveReport"},
            {10103, nullptr, "SaveReportWithUser"},
            {10200, nullptr, "RequestImmediateTransmission"},
            {10300, nullptr, "GetTransmissionStatus"},
            {20100, nullptr, "SaveSystemReport"},
            {20101, nullptr, "SaveSystemReportWithUser"},
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
    void SaveReportWithUserOld(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto user_id = rp.PopRaw<u128>();
        const auto process_id = rp.PopRaw<u64>();

        const auto data1 = ctx.ReadBuffer(0);
        const auto data2 = ctx.ReadBuffer(1);

        LOG_DEBUG(
            Service_PREPO,
            "called, user_id={:016X}{:016X}, unk1={:016X}, data1_size={:016X}, data2_size={:016X}",
            user_id[1], user_id[0], process_id, data1.size(), data2.size());

        const auto& reporter{Core::System::GetInstance().GetReporter()};
        reporter.SavePlayReport(Core::CurrentProcess()->GetTitleID(), process_id, {data1, data2},
                                user_id);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }
};

void InstallInterfaces(SM::ServiceManager& service_manager) {
    std::make_shared<PlayReport>("prepo:a")->InstallAsService(service_manager);
    std::make_shared<PlayReport>("prepo:a2")->InstallAsService(service_manager);
    std::make_shared<PlayReport>("prepo:m")->InstallAsService(service_manager);
    std::make_shared<PlayReport>("prepo:s")->InstallAsService(service_manager);
    std::make_shared<PlayReport>("prepo:u")->InstallAsService(service_manager);
}

} // namespace Service::PlayReport
