// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/service/service.h"

namespace Service::BCAT {

class Backend;

class Module final {
public:
    class Interface : public ServiceFramework<Interface> {
    public:
        explicit Interface(std::shared_ptr<Module> module, const char* name);
        ~Interface() override;

        void CreateBcatService(Kernel::HLERequestContext& ctx);
        void CreateDeliveryCacheStorageService(Kernel::HLERequestContext& ctx);
        void CreateDeliveryCacheStorageServiceWithApplicationId(Kernel::HLERequestContext& ctx);

    protected:
        std::shared_ptr<Module> module;
        std::unique_ptr<Backend> backend;
    };
};

/// Registers all BCAT services with the specified service manager.
void InstallInterfaces(SM::ServiceManager& service_manager);

} // namespace Service::BCAT
