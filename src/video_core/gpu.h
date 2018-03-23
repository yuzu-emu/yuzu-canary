// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <unordered_map>
#include <vector>
#include "common/common_types.h"
#include "video_core/memory_manager.h"

namespace Tegra {

/**
 * Struct describing framebuffer configuration
 */
struct FramebufferConfig {
    enum class PixelFormat : u32 {
        ABGR8 = 1,
    };

    /**
     * Returns the number of bytes per pixel.
     */
    static u32 BytesPerPixel(PixelFormat format) {
        switch (format) {
        case PixelFormat::ABGR8:
            return 4;
        }

        UNREACHABLE();
    }

    VAddr address;
    u32 offset;
    u32 width;
    u32 height;
    u32 stride;
    PixelFormat pixel_format;
    bool flip_vertical;
};

namespace Engines {
class Fermi2D;
class Maxwell3D;
class MaxwellCompute;
} // namespace Engines

enum class EngineID {
    FERMI_TWOD_A = 0x902D, // 2D Engine
    MAXWELL_B = 0xB197,    // 3D Engine
    MAXWELL_COMPUTE_B = 0xB1C0,
    KEPLER_INLINE_TO_MEMORY_B = 0xA140,
    MAXWELL_DMA_COPY_A = 0xB0B5,
};

class GPU final {
public:
    GPU();
    ~GPU();

    /// Processes a command list stored at the specified address in GPU memory.
    void ProcessCommandList(GPUVAddr address, u32 size);

    std::unique_ptr<MemoryManager> memory_manager;

    Engines::Maxwell3D& Maxwell3D() {
        return *maxwell_3d;
    }

private:
    static constexpr u32 InvalidGraphMacroEntry = 0xFFFFFFFF;

    /// Writes a single register in the engine bound to the specified subchannel
    void WriteReg(u32 method, u32 subchannel, u32 value, u32 remaining_params);

    /// Mapping of command subchannels to their bound engine ids.
    std::unordered_map<u32, EngineID> bound_engines;

    /// 3D engine
    std::unique_ptr<Engines::Maxwell3D> maxwell_3d;
    /// 2D engine
    std::unique_ptr<Engines::Fermi2D> fermi_2d;
    /// Compute engine
    std::unique_ptr<Engines::MaxwellCompute> maxwell_compute;

    /// Entry of the macro that is currently being uploaded
    u32 current_macro_entry = InvalidGraphMacroEntry;
    /// Code being uploaded for the current macro
    std::vector<u32> current_macro_code;
};

} // namespace Tegra
