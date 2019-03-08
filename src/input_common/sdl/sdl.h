// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <vector>
#include "core/frontend/input.h"
#include "input_common/main.h"

union SDL_Event;

namespace Common {
class ParamPackage;
} // namespace Common

namespace InputCommon::Polling {
class DevicePoller;
enum class DeviceType;
} // namespace InputCommon::Polling

namespace InputCommon::SDL {

class State {
public:
    /// Unresisters SDL device factories and shut them down.
    virtual ~State() = default;

    virtual std::vector<std::unique_ptr<InputCommon::Polling::DevicePoller>> GetPollers(
        InputCommon::Polling::DeviceType type) = 0;
};

class NullState : public State {
public:
    std::vector<std::unique_ptr<InputCommon::Polling::DevicePoller>> GetPollers(
        InputCommon::Polling::DeviceType type) override {}
};

std::unique_ptr<State> Init();

} // namespace InputCommon::SDL
