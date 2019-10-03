// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/hle/service/bcat/bcat.h"

namespace Service::BCAT {

BCAT::BCAT(std::shared_ptr<Module> module, FileSystem::FileSystemController& fsc, const char* name)
    : Module::Interface(std::move(module), fsc, name) {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &BCAT::CreateBcatService, "CreateBcatService"},
        {1, &BCAT::CreateDeliveryCacheStorageService, "CreateDeliveryCacheStorageService"},
        {2, &BCAT::CreateDeliveryCacheStorageServiceWithApplicationId, "CreateDeliveryCacheStorageServiceWithApplicationId"},
    };
    // clang-format on
    RegisterHandlers(functions);
}

BCAT::~BCAT() = default;
} // namespace Service::BCAT
