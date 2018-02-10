// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/service/apm/apm.h"
#include "core/hle/service/apm/interface.h"

namespace Service {
namespace APM {

void InstallInterfaces(SM::ServiceManager& service_manager) {
    auto module_ = std::make_shared<Module>();
    std::make_shared<APM>(module_, "apm")->InstallAsService(service_manager);
    std::make_shared<APM>(module_, "apm:p")->InstallAsService(service_manager);
}

} // namespace APM
} // namespace Service
