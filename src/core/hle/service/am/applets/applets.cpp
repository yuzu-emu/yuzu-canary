// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/frontend/applets/software_keyboard.h"
#include "core/hle/service/am/applets/applets.h"

namespace Service::AM::Applets {

std::shared_ptr<Frontend::SoftwareKeyboardApplet> software_keyboard =
    std::make_shared<Frontend::DefaultSoftwareKeyboardApplet>();

void Applet::Initialize(std::vector<std::shared_ptr<IStorage>> storage) {
    storage_stack = std::move(storage);
    initialized = true;
}

void RegisterSoftwareKeyboard(std::shared_ptr<Frontend::SoftwareKeyboardApplet> applet) {
    if (applet == nullptr)
        return;

    software_keyboard = std::move(applet);
}

std::shared_ptr<Frontend::SoftwareKeyboardApplet> GetSoftwareKeyboard() {
    return software_keyboard;
}

} // namespace Service::AM::Applets
