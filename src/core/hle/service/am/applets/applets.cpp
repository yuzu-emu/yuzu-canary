// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/frontend/applets/software_keyboard.h"
#include "core/hle/service/am/applets/applets.h"

namespace Service::AM::Applets {

Applet::Applet() = default;

Applet::~Applet() = default;

void Applet::Initialize(std::vector<std::shared_ptr<IStorage>> storage) {
    storage_stack = std::move(storage);
    initialized = true;
}

} // namespace Service::AM::Applets
