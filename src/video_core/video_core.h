// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>

namespace Core {
class System;
}

namespace Core::Frontend {
class EmuWindow;
}

namespace VideoCore {

class RendererBase;

/**
 * Creates a renderer instance.
 *
 * @note The returned renderer instance is simply allocated. Its Init()
 *       function still needs to be called to fully complete its setup.
 */
std::unique_ptr<RendererBase> CreateRenderer(Core::Frontend::EmuWindow& emu_window,
                                             Core::System& system);

u16 GetResolutionScaleFactor(const RendererBase& renderer);

} // namespace VideoCore
