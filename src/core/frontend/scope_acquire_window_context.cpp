// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/frontend/emu_window.h"
#include "core/frontend/scope_acquire_window_context.h"

namespace Core::Frontend {

ScopeAcquireWindowContext::ScopeAcquireWindowContext(Core::Frontend::EmuWindow& emu_window_)
    : emu_window{emu_window_} {
    emu_window.MakeCurrent();
}
ScopeAcquireWindowContext::~ScopeAcquireWindowContext() {
    emu_window.DoneCurrent();
}

} // namespace Core::Frontend
