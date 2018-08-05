// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "audio_core/audio_renderer.h"
#include "core/hle/service/service.h"

namespace Kernel {
class HLERequestContext;
}

namespace Service::Audio {

class AudRenU final : public ServiceFramework<AudRenU> {
public:
    explicit AudRenU();
    ~AudRenU() = default;

private:
    void OpenAudioRenderer(Kernel::HLERequestContext& ctx);
    void GetAudioRendererWorkBufferSize(Kernel::HLERequestContext& ctx);
    void GetAudioDevice(Kernel::HLERequestContext& ctx);

    enum class AudioFeatures : u32 {
        Splitter,
    };

    bool IsFeatureSupported(AudioFeatures feature, u32_le revision) const;
};

} // namespace Service::Audio
