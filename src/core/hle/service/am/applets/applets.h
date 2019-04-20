// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <queue>
#include "common/swap.h"
#include "core/hle/kernel/object.h"
#include "core/hle/kernel/writable_event.h"

union ResultCode;

namespace Core::Frontend {
class ErrorApplet;
class PhotoViewerApplet;
class ProfileSelectApplet;
class SoftwareKeyboardApplet;
class WebBrowserApplet;
} // namespace Core::Frontend

namespace Service::AM {

class IStorage;

namespace Applets {

enum class AppletId : u32 {
    OverlayDisplay = 0x02,
    QLaunch = 0x03,
    Starter = 0x04,
    Auth = 0x0A,
    Cabinet = 0x0B,
    Controller = 0x0C,
    DataErase = 0x0D,
    Error = 0x0E,
    NetConnect = 0x0F,
    ProfileSelect = 0x10,
    SoftwareKeyboard = 0x11,
    MiiEdit = 0x12,
    LibAppletWeb = 0x13,
    LibAppletShop = 0x14,
    PhotoViewer = 0x15,
    Settings = 0x16,
    LibAppletOff = 0x17,
    LibAppletWhitelisted = 0x18,
    LibAppletAuth = 0x19,
    MyPage = 0x1A,
};

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

    Kernel::SharedPtr<Kernel::ReadableEvent> GetNormalDataEvent() const;
    Kernel::SharedPtr<Kernel::ReadableEvent> GetInteractiveDataEvent() const;
    Kernel::SharedPtr<Kernel::ReadableEvent> GetStateChangedEvent() const;

private:
    // Queues are named from applet's perspective

    // PopNormalDataToApplet and PushNormalDataFromGame
    std::queue<std::unique_ptr<IStorage>> in_channel;

    // PopNormalDataToGame and PushNormalDataFromApplet
    std::queue<std::unique_ptr<IStorage>> out_channel;

    // PopInteractiveDataToApplet and PushInteractiveDataFromGame
    std::queue<std::unique_ptr<IStorage>> in_interactive_channel;

    // PopInteractiveDataToGame and PushInteractiveDataFromApplet
    std::queue<std::unique_ptr<IStorage>> out_interactive_channel;

    Kernel::EventPair state_changed_event;

    // Signaled on PushNormalDataFromApplet
    Kernel::EventPair pop_out_data_event;

    // Signaled on PushInteractiveDataFromApplet
    Kernel::EventPair pop_interactive_out_data_event;
};

class Applet {
public:
    Applet();
    virtual ~Applet();

    virtual void Initialize();

    virtual bool TransactionComplete() const = 0;
    virtual ResultCode GetStatus() const = 0;
    virtual void ExecuteInteractive() = 0;
    virtual void Execute() = 0;

    bool IsInitialized() const {
        return initialized;
    }

    AppletDataBroker& GetBroker() {
        return broker;
    }

    const AppletDataBroker& GetBroker() const {
        return broker;
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

    CommonArguments common_args{};
    AppletDataBroker broker;
    bool initialized = false;
};

struct AppletFrontendSet {
    std::unique_ptr<Core::Frontend::ErrorApplet> error;
    std::unique_ptr<Core::Frontend::PhotoViewerApplet> photo_viewer;
    std::unique_ptr<Core::Frontend::ProfileSelectApplet> profile_select;
    std::unique_ptr<Core::Frontend::SoftwareKeyboardApplet> software_keyboard;
    std::unique_ptr<Core::Frontend::WebBrowserApplet> web_browser;
};

class AppletManager {
public:
    AppletManager();
    ~AppletManager();

    void SetAppletFrontendSet(AppletFrontendSet set);
    void SetDefaultAppletFrontendSet();
    void SetDefaultAppletsIfMissing();
    void ClearAll();

    std::shared_ptr<Applet> GetApplet(AppletId id) const;

private:
    AppletFrontendSet frontend;
};

} // namespace Applets
} // namespace Service::AM
