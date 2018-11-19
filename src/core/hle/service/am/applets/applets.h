// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <memory>
#include <queue>
#include "common/swap.h"
#include "core/hle/kernel/event.h"

union ResultCode;

namespace Service::AM {

class IStorage;

namespace Applets {

class AppletDataBroker final {
public:
    AppletDataBroker();
    ~AppletDataBroker();

    std::unique_ptr<IStorage> PopNormalDataToGame();
    std::unique_ptr<IStorage> PopNormalDataToApplet();

    std::unique_ptr<IStorage> PopInteractiveDataToGame();
    std::unique_ptr<IStorage> PopInteractiveDataToApplet();

    void PushNormalDataFromGame(IStorage storage);
    void PushNormalDataFromApplet(IStorage storage);

    void PushInteractiveDataFromGame(IStorage storage);
    void PushInteractiveDataFromApplet(IStorage storage);

    void SignalStateChanged() const;

    Kernel::SharedPtr<Kernel::Event> GetNormalDataEvent() const;
    Kernel::SharedPtr<Kernel::Event> GetInteractiveDataEvent() const;
    Kernel::SharedPtr<Kernel::Event> GetStateChangedEvent() const;

private:
    // Queues are named from applet's perspective
    std::queue<std::unique_ptr<IStorage>>
        in_channel; // PopNormalDataToApplet and PushNormalDataFromGame
    std::queue<std::unique_ptr<IStorage>>
        out_channel; // PopNormalDataToGame and PushNormalDataFromApplet
    std::queue<std::unique_ptr<IStorage>>
        in_interactive_channel; // PopInteractiveDataToApplet and PushInteractiveDataFromGame
    std::queue<std::unique_ptr<IStorage>>
        out_interactive_channel; // PopInteractiveDataToGame and PushInteractiveDataFromApplet

    Kernel::SharedPtr<Kernel::Event> state_changed_event;
    Kernel::SharedPtr<Kernel::Event> pop_out_data_event; // Signaled on PushNormalDataFromApplet
    Kernel::SharedPtr<Kernel::Event>
        pop_interactive_out_data_event; // Signaled on PushInteractiveDataFromApplet
};

class Applet {
public:
    Applet();
    virtual ~Applet();

    virtual void Initialize(std::shared_ptr<AppletDataBroker> broker);

    virtual bool TransactionComplete() const = 0;
    virtual ResultCode GetStatus() const = 0;
    virtual void ExecuteInteractive() = 0;
    virtual void Execute() = 0;

    bool IsInitialized() const {
        return initialized;
    }

protected:
    struct CommonArguments {
        u32_le arguments_version;
        u32_le size;
        u32_le library_version;
        u32_le theme_color;
        u8 play_startup_sound;
        u64_le system_tick;
    };
    static_assert(sizeof(CommonArguments) == 0x20, "CommonArguments has incorrect size.");

    CommonArguments common_args;
    std::shared_ptr<AppletDataBroker> broker;
    bool initialized = false;
};

} // namespace Applets
} // namespace Service::AM
