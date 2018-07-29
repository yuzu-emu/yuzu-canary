// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <vector>
#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/event.h"
#include "core/hle/kernel/hle_ipc.h"
#include "core/hle/service/audio/audout_u.h"

namespace Service::Audio {

namespace ErrCodes {
enum {
    ErrorUnknown = 2,
    BufferCountExceeded = 8,
};
}

constexpr std::array<char, 10> DefaultDevice{{"DeviceOut"}};
constexpr int DefaultSampleRate{48000};

class IAudioOut final : public ServiceFramework<IAudioOut> {
public:
    IAudioOut(AudoutParams audio_params, AudioCore::AudioOut& audio_core)
        : ServiceFramework("IAudioOut"), audio_params(audio_params), audio_core(audio_core) {

        static const FunctionInfo functions[] = {
            {0, &IAudioOut::GetAudioOutState, "GetAudioOutState"},
            {1, &IAudioOut::StartAudioOut, "StartAudioOut"},
            {2, &IAudioOut::StopAudioOut, "StopAudioOut"},
            {3, &IAudioOut::AppendAudioOutBufferImpl, "AppendAudioOutBuffer"},
            {4, &IAudioOut::RegisterBufferEvent, "RegisterBufferEvent"},
            {5, &IAudioOut::GetReleasedAudioOutBufferImpl, "GetReleasedAudioOutBuffer"},
            {6, &IAudioOut::ContainsAudioOutBuffer, "ContainsAudioOutBuffer"},
            {7, &IAudioOut::AppendAudioOutBufferImpl, "AppendAudioOutBufferAuto"},
            {8, &IAudioOut::GetReleasedAudioOutBufferImpl, "GetReleasedAudioOutBufferAuto"},
            {9, &IAudioOut::GetAudioOutBufferCount, "GetAudioOutBufferCount"},
            {10, nullptr, "GetAudioOutPlayedSampleCount"},
            {11, nullptr, "FlushAudioOutBuffers"},
        };
        RegisterHandlers(functions);

        // This is the event handle used to check if the audio buffer was released
        buffer_event = Kernel::Event::Create(Kernel::ResetType::Sticky, "IAudioOutBufferReleased");

        stream = audio_core.OpenStream(audio_params.sample_rate, audio_params.channel_count,
                                       [=]() { buffer_event->Signal(); });
    }

private:
    struct AudioBuffer {
        u64_le next;
        u64_le buffer;
        u64_le buffer_capacity;
        u64_le buffer_size;
        u64_le offset;
    };
    static_assert(sizeof(AudioBuffer) == 0x28, "AudioBuffer is an invalid size");

    void GetAudioOutState(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_Audio, "called");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push(static_cast<u32>(stream->IsPlaying() ? AudioState::Started : AudioState::Stopped));
    }

    void StartAudioOut(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_Audio, "called");

        if (stream->IsPlaying()) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultCode(ErrorModule::Audio, ErrCodes::ErrorUnknown));
            return;
        }

        audio_core.StartStream(stream);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void StopAudioOut(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_Audio, "called");

        audio_core.StopStream(stream);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void RegisterBufferEvent(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_Audio, "called");

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushCopyObjects(buffer_event);
    }

    void AppendAudioOutBufferImpl(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_Audio, "(STUBBED) called {}", ctx.Description());
        IPC::RequestParser rp{ctx};

        const auto& input_buffer{ctx.ReadBuffer()};
        ASSERT_MSG(input_buffer.size() == sizeof(AudioBuffer),
                   "AudioBuffer input is an invalid size!");
        AudioBuffer audio_buffer{};
        std::memcpy(&audio_buffer, input_buffer.data(), sizeof(AudioBuffer));
        const u64 tag{rp.Pop<u64>()};

        std::vector<u8> data(audio_buffer.buffer_size);
        Memory::ReadBlock(audio_buffer.buffer, data.data(), data.size());

        if (!audio_core.QueueBuffer(stream, tag, std::move(data))) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultCode(ErrorModule::Audio, ErrCodes::BufferCountExceeded));
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void GetReleasedAudioOutBufferImpl(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_Audio, "called {}", ctx.Description());
        IPC::RequestParser rp{ctx};
        const u64 max_count{ctx.GetWriteBufferSize() / sizeof(u64)};
        const auto released_buffers{audio_core.GetTagsAndReleaseBuffers(stream, max_count)};

        std::vector<u64> tags{released_buffers};
        tags.resize(max_count);
        ctx.WriteBuffer(tags);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(static_cast<u32>(released_buffers.size()));
    }

    void ContainsAudioOutBuffer(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_Audio, "called");
        IPC::RequestParser rp{ctx};
        const u64 tag{rp.Pop<u64>()};
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push(stream->ContainsBuffer(tag));
    }

    void GetAudioOutBufferCount(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_Audio, "called");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push(static_cast<u32>(stream->GetQueueSize()));
    }

    AudioCore::AudioOut& audio_core;
    AudioCore::StreamPtr stream;

    AudoutParams audio_params{};

    /// This is the evend handle used to check if the audio buffer was released
    Kernel::SharedPtr<Kernel::Event> buffer_event;
};

void AudOutU::ListAudioOutsImpl(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_Audio, "called");
    IPC::RequestParser rp{ctx};

    ctx.WriteBuffer(DefaultDevice);

    IPC::ResponseBuilder rb = rp.MakeBuilder(3, 0, 0);

    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(1); // Amount of audio devices
}

void AudOutU::OpenAudioOutImpl(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_Audio, "called");

    ctx.WriteBuffer(DefaultDevice);
    IPC::RequestParser rp{ctx};
    auto params{rp.PopRaw<AudoutParams>()};
    if (params.channel_count <= 2) {
        // Mono does not exist for audout
        params.channel_count = 2;
    } else {
        params.channel_count = 6;
    }
    if (!params.sample_rate) {
        params.sample_rate = DefaultSampleRate;
    }

    // TODO(bunnei): Support more than one IAudioOut interface. When we add this, ListAudioOutsImpl
    // will likely need to be updated as well.
    ASSERT_MSG(!audio_out_interface, "Unimplemented");
    audio_out_interface = std::make_shared<IAudioOut>(std::move(params), *audio_core);

    IPC::ResponseBuilder rb{ctx, 6, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(DefaultSampleRate);
    rb.Push<u32>(params.channel_count);
    rb.Push<u32>(static_cast<u32>(PcmFormat::Int16));
    rb.Push<u32>(static_cast<u32>(AudioState::Stopped));
    rb.PushIpcInterface<Audio::IAudioOut>(audio_out_interface);
}

AudOutU::AudOutU() : ServiceFramework("audout:u") {
    static const FunctionInfo functions[] = {{0, &AudOutU::ListAudioOutsImpl, "ListAudioOuts"},
                                             {1, &AudOutU::OpenAudioOutImpl, "OpenAudioOut"},
                                             {2, &AudOutU::ListAudioOutsImpl, "ListAudioOutsAuto"},
                                             {3, &AudOutU::OpenAudioOutImpl, "OpenAudioOutAuto"}};
    RegisterHandlers(functions);
    audio_core = std::make_unique<AudioCore::AudioOut>();
}

} // namespace Service::Audio
