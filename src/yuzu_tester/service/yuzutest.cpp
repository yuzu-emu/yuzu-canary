// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include "common/string_util.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/sm.h"
#include "yuzu_tester/service/yuzutest.h"

namespace Service::Yuzu {

constexpr u64 SERVICE_VERSION = 0x00000002;

class YuzuTest final : public ServiceFramework<YuzuTest> {
public:
    explicit YuzuTest(std::string data,
                      std::function<void(std::vector<TestResult>)> finish_callback)
        : ServiceFramework{"yuzutest"}, data(std::move(data)),
          finish_callback(std::move(finish_callback)) {
        static const FunctionInfo functions[] = {
            {0, &YuzuTest::Initialize, "Initialize"},
            {1, &YuzuTest::GetServiceVersion, "GetServiceVersion"},
            {2, &YuzuTest::GetData, "GetData"},
            {10, &YuzuTest::StartIndividual, "StartIndividual"},
            {20, &YuzuTest::FinishIndividual, "FinishIndividual"},
            {100, &YuzuTest::ExitProgram, "ExitProgram"},
        };

        RegisterHandlers(functions);
    }

private:
    void Initialize(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Frontend, "called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void GetServiceVersion(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Frontend, "called");
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(RESULT_SUCCESS);
        rb.Push(SERVICE_VERSION);
    }

    void GetData(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Frontend, "called");
        const auto size = ctx.GetWriteBufferSize();
        const auto write_size = std::min(size, data.size());
        ctx.WriteBuffer(data.data(), write_size);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(write_size);
    }

    void StartIndividual(Kernel::HLERequestContext& ctx) {
        const auto name_raw = ctx.ReadBuffer();

        const auto name = Common::StringFromFixedZeroTerminatedBuffer(
            reinterpret_cast<const char*>(name_raw.data()), name_raw.size());

        LOG_DEBUG(Frontend, "called, name={}", name);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void FinishIndividual(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};

        const auto code = rp.PopRaw<u32>();

        const auto result_data_raw = ctx.ReadBuffer();
        const auto test_name_raw = ctx.ReadBuffer(1);

        const auto data = Common::StringFromFixedZeroTerminatedBuffer(
            reinterpret_cast<const char*>(result_data_raw.data()), result_data_raw.size());
        const auto test_name = Common::StringFromFixedZeroTerminatedBuffer(
            reinterpret_cast<const char*>(test_name_raw.data()), test_name_raw.size());

        LOG_INFO(Frontend, "called, result_code={:08X}, data={}, name={}", code, data, test_name);

        results.push_back({code, data, test_name});

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void ExitProgram(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Frontend, "called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);

        finish_callback(results);
    }

    std::string data;

    std::vector<TestResult> results;
    std::function<void(std::vector<TestResult>)> finish_callback;
};

void InstallInterfaces(SM::ServiceManager& sm, std::string data,
                       std::function<void(std::vector<TestResult>)> finish_callback) {
    std::make_shared<YuzuTest>(data, finish_callback)->InstallAsService(sm);
}

} // namespace Service::Yuzu
