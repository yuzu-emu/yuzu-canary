// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/hle/service/acc/acc_aa.h"

namespace Service::Account {

ACC_AA::ACC_AA(std::shared_ptr<Module> module, std::shared_ptr<ProfileManager> profile_manager)
    : Module::Interface(std::move(module), std::move(profile_manager), "acc:aa") {
    static const FunctionInfo functions[] = {
        {0, nullptr, "EnsureCacheAsync"},
        {1, nullptr, "LoadCache"},
        {2, nullptr, "GetDeviceAccountId"},
        {50, nullptr, "RegisterNotificationTokenAsync"},
        {51, nullptr, "UnregisterNotificationTokenAsync"},
    };
    RegisterHandlers(functions);
}

} // namespace Service::Account
