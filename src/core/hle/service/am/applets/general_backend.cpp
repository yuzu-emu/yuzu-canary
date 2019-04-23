// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <string>

#include "common/assert.h"
#include "common/hex_util.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/frontend/applets/general_frontend.h"
#include "core/hle/kernel/process.h"
#include "core/hle/result.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/am/applets/general_backend.h"

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

PhotoViewer::PhotoViewer(const Core::Frontend::PhotoViewerApplet& frontend) : frontend(frontend) {}

PhotoViewer::~PhotoViewer() = default;

void PhotoViewer::Initialize() {
    Applet::Initialize();
    complete = false;

    const auto storage = broker.PopNormalDataToApplet();
    ASSERT(storage != nullptr);
    const auto data = storage->GetData();
    ASSERT(!data.empty());
    mode = static_cast<PhotoViewerAppletMode>(data[0]);
}

bool PhotoViewer::TransactionComplete() const {
    return complete;
}

ResultCode PhotoViewer::GetStatus() const {
    return RESULT_SUCCESS;
}

void PhotoViewer::ExecuteInteractive() {
    UNREACHABLE_MSG("Unexpected interactive applet data.");
}

void PhotoViewer::Execute() {
    if (complete)
        return;

    const auto callback = [this] { ViewFinished(); };
    switch (mode) {
    case PhotoViewerAppletMode::CurrentApp:
        frontend.ShowPhotosForApplication(Core::CurrentProcess()->GetTitleID(), callback);
        break;
    case PhotoViewerAppletMode::AllApps:
        frontend.ShowAllPhotos(callback);
        break;
    default:
        UNIMPLEMENTED_MSG("Unimplemented PhotoViewer applet mode={:02X}!", static_cast<u8>(mode));
    }
}

void PhotoViewer::ViewFinished() {
    broker.PushNormalDataFromApplet(IStorage{{}});
    broker.SignalStateChanged();
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
