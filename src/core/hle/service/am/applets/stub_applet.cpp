// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/hex_util.h"
#include "core/hle/service/am/applets/stub_applet.h"

namespace Service::AM::Applets {

static void LogCurrentStorage(AppletDataBroker& broker, std::string prefix) {
    std::unique_ptr<IStorage> storage = broker.PopNormalDataToApplet();
    for (; storage != nullptr; storage = broker.PopNormalDataToApplet()) {
        const auto data = storage->GetData();
        LOG_INFO(Service_AM,
                 "called (STUBBED), during {} recieved normal data with size={:08X}, data={}",
                 prefix, data.size(), Common::HexVectorToString(data));
    }

    storage = broker.PopInteractiveDataToApplet();
    for (; storage != nullptr; storage = broker.PopInteractiveDataToApplet()) {
        const auto data = storage->GetData();
        LOG_INFO(Service_AM,
                 "called (STUBBED), during {} recieved interactive data with size={:08X}, data={}",
                 prefix, data.size(), Common::HexVectorToString(data));
    }
}

StubApplet::StubApplet() = default;

StubApplet::~StubApplet() = default;

void StubApplet::Initialize() {
    LOG_WARNING(Service_AM, "called (STUBBED)");
    Applet::Initialize();
    LogCurrentStorage(broker, "Initialize");
}

bool StubApplet::TransactionComplete() const {
    LOG_WARNING(Service_AM, "called (STUBBED)");
    return true;
}

ResultCode StubApplet::GetStatus() const {
    LOG_WARNING(Service_AM, "called (STUBBED)");
    return RESULT_SUCCESS;
}

void StubApplet::ExecuteInteractive() {
    LOG_WARNING(Service_AM, "called (STUBBED)");
    LogCurrentStorage(broker, "ExecuteInteractive");

    broker.PushNormalDataFromApplet(IStorage{std::vector<u8>(0x1000)});
    broker.PushInteractiveDataFromApplet(IStorage{std::vector<u8>(0x1000)});
    broker.SignalStateChanged();
}

void StubApplet::Execute() {
    LOG_WARNING(Service_AM, "called (STUBBED)");
    LogCurrentStorage(broker, "Execute");

    broker.PushNormalDataFromApplet(IStorage{std::vector<u8>(0x1000)});
    broker.PushInteractiveDataFromApplet(IStorage{std::vector<u8>(0x1000)});
    broker.SignalStateChanged();
}
} // namespace Service::AM::Applets
