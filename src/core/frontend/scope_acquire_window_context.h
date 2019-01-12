// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"

namespace Core::Frontend {

class EmuWindow;

/// Helper class to acquire/release window context within a given scope
class ScopeAcquireWindowContext : NonCopyable {
public:
    explicit ScopeAcquireWindowContext(Core::Frontend::EmuWindow& window);
    ~ScopeAcquireWindowContext();

private:
    Core::Frontend::EmuWindow& emu_window;
};

} // namespace Core::Frontend
