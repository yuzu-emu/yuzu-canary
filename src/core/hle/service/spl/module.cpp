// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <vector>
#include "common/logging/log.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/service/spl/csrng.h"
#include "core/hle/service/spl/module.h"
#include "core/hle/service/spl/spl.h"
#include "core/settings.h"

namespace Service::SPL {

Module::Interface::Interface(std::shared_ptr<Module> module, const char* name)
    : ServiceFramework(name), module(std::move(module)),
      rng(Settings::values.rng_seed.value_or(std::time(nullptr))) {}

Module::Interface::~Interface() = default;

void Module::Interface::GetRandomBytes(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};

    std::size_t size = ctx.GetWriteBufferSize();

    std::uniform_int_distribution<u16> distribution(0, std::numeric_limits<u8>::max());
    std::vector<u8> data(size);
    std::generate(data.begin(), data.end(), [&] { return static_cast<u8>(distribution(rng)); });

    ctx.WriteBuffer(data);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    LOG_DEBUG(Service_SPL, "called");
}

void InstallInterfaces(SM::ServiceManager& service_manager) {
    auto module = std::make_shared<Module>();
    std::make_shared<CSRNG>(module)->InstallAsService(service_manager);
    std::make_shared<SPL>(module)->InstallAsService(service_manager);
}

} // namespace Service::SPL
