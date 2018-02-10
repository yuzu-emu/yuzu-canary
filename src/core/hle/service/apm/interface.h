// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/service/service.h"

namespace Service {
namespace APM {

class APM final : public ServiceFramework<APM> {
public:
    APM(std::shared_ptr<Module> apm, const char* name);
    ~APM() = default;

private:
    void OpenSession(Kernel::HLERequestContext& ctx);

    std::shared_ptr<Module> apm;
};

/// Registers all AM services with the specified service manager.
void InstallInterfaces(SM::ServiceManager& service_manager);

} // namespace APM
} // namespace Service
