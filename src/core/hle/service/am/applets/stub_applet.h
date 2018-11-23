// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/service/am/applets/applets.h"

namespace Service::AM::Applets {

class StubApplet final : public Applet {
public:
    StubApplet();
    ~StubApplet() override;

    void Initialize() override;

    bool TransactionComplete() const override;
    ResultCode GetStatus() const override;
    void ExecuteInteractive() override;
    void Execute() override;
};

} // namespace Service::AM::Applets
