// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <vector>
#include "common/swap.h"

namespace Frontend {
class SoftwareKeyboardApplet;
}

namespace Service::AM {

class IStorage;

namespace Applets {

class Applet {
public:
    virtual void Initialize(std::vector<std::shared_ptr<IStorage>> storage);

    virtual IStorage Execute() = 0;

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

    std::vector<std::shared_ptr<IStorage>> storage_stack;
    bool initialized = false;
};

void RegisterSoftwareKeyboard(std::shared_ptr<Frontend::SoftwareKeyboardApplet> applet);
std::shared_ptr<Frontend::SoftwareKeyboardApplet> GetSoftwareKeyboard();

} // namespace Applets
} // namespace Service::AM
