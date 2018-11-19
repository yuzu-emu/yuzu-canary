// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include "common/assert.h"
#include "core/core.h"
#include "core/hle/kernel/event.h"
#include "core/hle/kernel/server_port.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/am/applets/applets.h"

namespace Service::AM::Applets {

AppletDataBroker::AppletDataBroker() {
    auto& kernel = Core::System::GetInstance().Kernel();
    state_changed_event = Kernel::Event::Create(kernel, Kernel::ResetType::OneShot,
                                                "ILibraryAppletAccessor:StateChangedEvent");
    pop_out_data_event = Kernel::Event::Create(kernel, Kernel::ResetType::OneShot,
                                               "ILibraryAppletAccessor:PopDataOutEvent");
    pop_interactive_out_data_event = Kernel::Event::Create(
        kernel, Kernel::ResetType::OneShot, "ILibraryAppletAccessor:PopInteractiveDataOutEvent");
}

AppletDataBroker::~AppletDataBroker() = default;

std::unique_ptr<IStorage> AppletDataBroker::PopNormalDataToGame() {
    if (out_channel.empty())
        return nullptr;

    auto out = std::move(out_channel.front());
    out_channel.pop();
    return out;
}

std::unique_ptr<IStorage> AppletDataBroker::PopNormalDataToApplet() {
    if (in_channel.empty())
        return nullptr;

    auto out = std::move(in_channel.front());
    in_channel.pop();
    return out;
}

std::unique_ptr<IStorage> AppletDataBroker::PopInteractiveDataToGame() {
    if (out_interactive_channel.empty())
        return nullptr;

    auto out = std::move(out_interactive_channel.front());
    out_interactive_channel.pop();
    return out;
}

std::unique_ptr<IStorage> AppletDataBroker::PopInteractiveDataToApplet() {
    if (in_interactive_channel.empty())
        return nullptr;

    auto out = std::move(in_interactive_channel.front());
    in_interactive_channel.pop();
    return out;
}

void AppletDataBroker::PushNormalDataFromGame(IStorage storage) {
    in_channel.push(std::make_unique<IStorage>(storage));
}

void AppletDataBroker::PushNormalDataFromApplet(IStorage storage) {
    out_channel.push(std::make_unique<IStorage>(storage));
    pop_out_data_event->Signal();
}

void AppletDataBroker::PushInteractiveDataFromGame(IStorage storage) {
    in_interactive_channel.push(std::make_unique<IStorage>(storage));
}

void AppletDataBroker::PushInteractiveDataFromApplet(IStorage storage) {
    out_interactive_channel.push(std::make_unique<IStorage>(storage));
    pop_interactive_out_data_event->Signal();
}

void AppletDataBroker::SignalStateChanged() const {
    state_changed_event->Signal();
}

Kernel::SharedPtr<Kernel::Event> AppletDataBroker::GetNormalDataEvent() const {
    return pop_out_data_event;
}

Kernel::SharedPtr<Kernel::Event> AppletDataBroker::GetInteractiveDataEvent() const {
    return pop_interactive_out_data_event;
}

Kernel::SharedPtr<Kernel::Event> AppletDataBroker::GetStateChangedEvent() const {
    return state_changed_event;
}

Applet::Applet() = default;

Applet::~Applet() = default;

void Applet::Initialize(std::shared_ptr<AppletDataBroker> broker_) {
    broker = std::move(broker_);

    const auto common = broker->PopNormalDataToApplet();
    ASSERT(common != nullptr);

    const auto common_data = common->GetData();

    ASSERT(common_data.size() >= sizeof(CommonArguments));
    std::memcpy(&common_args, common_data.data(), sizeof(CommonArguments));

    initialized = true;
}

} // namespace Service::AM::Applets
