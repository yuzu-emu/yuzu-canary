// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include "common/assert.h"
#include "core/frontend/applets/software_keyboard.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/am/applets/applets.h"

namespace Service::AM::Applets {

Applet::Applet() = default;

Applet::~Applet() = default;

void Applet::Initialize(std::queue<std::shared_ptr<IStorage>> storage) {
    storage_stack = std::move(storage);

    const auto common_data = storage_stack.front()->GetData();
    storage_stack.pop();

    ASSERT(common_data.size() >= sizeof(CommonArguments));
    std::memcpy(&common_args, common_data.data(), sizeof(CommonArguments));

    initialized = true;
}

} // namespace Service::AM::Applets
