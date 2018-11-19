// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <memory>
#include <queue>
#include "common/swap.h"

union ResultCode;

namespace Frontend {
class SoftwareKeyboardApplet;
}

namespace Service::AM {

class IStorage;

namespace Applets {

using AppletStorageProxyFunction = std::function<void(IStorage)>;
using AppletStateProxyFunction = std::function<void()>;

class Applet {
public:
    Applet();
    virtual ~Applet();

    virtual void Initialize(std::queue<std::shared_ptr<IStorage>> storage);

    virtual bool TransactionComplete() const = 0;
    virtual ResultCode GetStatus() const = 0;
    virtual void ReceiveInteractiveData(std::shared_ptr<IStorage> storage) = 0;
    virtual void Execute(AppletStorageProxyFunction out_data,
                         AppletStorageProxyFunction out_interactive_data,
                         AppletStateProxyFunction state) = 0;

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
    std::queue<std::shared_ptr<IStorage>> storage_stack;
    bool initialized = false;
};

} // namespace Applets
} // namespace Service::AM
