// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "audio_core/audio_out.h"
#include "core/hle/service/service.h"

namespace Kernel {
class HLERequestContext;
}

namespace Service::Audio {

struct AudoutParams {
    s32_le sample_rate;
    u16_le channel_count;
    INSERT_PADDING_BYTES(2);
};
static_assert(sizeof(AudoutParams) == 0x8, "AudoutParams is an invalid size");

enum class AudioState : u32 {
    Started,
    Stopped,
};

class IAudioOut;

class AudOutU final : public ServiceFramework<AudOutU> {
public:
    AudOutU();
    ~AudOutU() = default;

private:
    std::shared_ptr<IAudioOut> audio_out_interface;
    std::unique_ptr<AudioCore::AudioOut> audio_core;

    void ListAudioOutsImpl(Kernel::HLERequestContext& ctx);
    void OpenAudioOutImpl(Kernel::HLERequestContext& ctx);

    enum class PcmFormat : u32 {
        Invalid = 0,
        Int8 = 1,
        Int16 = 2,
        Int24 = 3,
        Int32 = 4,
        PcmFloat = 5,
        Adpcm = 6,
    };
};

} // namespace Service::Audio
