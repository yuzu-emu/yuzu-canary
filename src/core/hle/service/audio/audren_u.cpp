// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <memory>

#include "audio_core/audio_renderer.h"
#include "common/alignment.h"
#include "common/common_funcs.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/hle_ipc.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/readable_event.h"
#include "core/hle/kernel/writable_event.h"
#include "core/hle/service/audio/audren_u.h"

namespace Service::Audio {

class IAudioRenderer final : public ServiceFramework<IAudioRenderer> {
public:
    explicit IAudioRenderer(AudioCore::AudioRendererParameter audren_params)
        : ServiceFramework("IAudioRenderer") {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IAudioRenderer::GetSampleRate, "GetSampleRate"},
            {1, &IAudioRenderer::GetSampleCount, "GetSampleCount"},
            {2, &IAudioRenderer::GetMixBufferCount, "GetMixBufferCount"},
            {3, &IAudioRenderer::GetState, "GetState"},
            {4, &IAudioRenderer::RequestUpdateImpl, "RequestUpdate"},
            {5, &IAudioRenderer::Start, "Start"},
            {6, &IAudioRenderer::Stop, "Stop"},
            {7, &IAudioRenderer::QuerySystemEvent, "QuerySystemEvent"},
            {8, &IAudioRenderer::SetRenderingTimeLimit, "SetRenderingTimeLimit"},
            {9, &IAudioRenderer::GetRenderingTimeLimit, "GetRenderingTimeLimit"},
            {10, &IAudioRenderer::RequestUpdateImpl, "RequestUpdateAuto"},
            {11, nullptr, "ExecuteAudioRendererRendering"},
        };
        // clang-format on
        RegisterHandlers(functions);

        auto& system = Core::System::GetInstance();
        system_event = Kernel::WritableEvent::CreateEventPair(
            system.Kernel(), Kernel::ResetType::Sticky, "IAudioRenderer:SystemEvent");
        renderer = std::make_unique<AudioCore::AudioRenderer>(system, audren_params,
                                                              system_event.writable);
    }

private:
    void UpdateAudioCallback() {
        system_event.writable->Signal();
    }

    void GetSampleRate(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_Audio, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(renderer->GetSampleRate());
    }

    void GetSampleCount(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_Audio, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(renderer->GetSampleCount());
    }

    void GetState(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_Audio, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(static_cast<u32>(renderer->GetStreamState()));
    }

    void GetMixBufferCount(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_Audio, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(renderer->GetMixBufferCount());
    }

    void RequestUpdateImpl(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_Audio, "(STUBBED) called");

        ctx.WriteBuffer(renderer->UpdateAudioRenderer(ctx.ReadBuffer()));
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void Start(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_Audio, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};

        rb.Push(RESULT_SUCCESS);
    }

    void Stop(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_Audio, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};

        rb.Push(RESULT_SUCCESS);
    }

    void QuerySystemEvent(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_Audio, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushCopyObjects(system_event.readable);
    }

    void SetRenderingTimeLimit(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        rendering_time_limit_percent = rp.Pop<u32>();
        LOG_DEBUG(Service_Audio, "called. rendering_time_limit_percent={}",
                  rendering_time_limit_percent);

        ASSERT(rendering_time_limit_percent >= 0 && rendering_time_limit_percent <= 100);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void GetRenderingTimeLimit(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_Audio, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push(rendering_time_limit_percent);
    }

    Kernel::EventPair system_event;
    std::unique_ptr<AudioCore::AudioRenderer> renderer;
    u32 rendering_time_limit_percent = 100;
};

class IAudioDevice final : public ServiceFramework<IAudioDevice> {
public:
    IAudioDevice() : ServiceFramework("IAudioDevice") {
        static const FunctionInfo functions[] = {
            {0, &IAudioDevice::ListAudioDeviceName, "ListAudioDeviceName"},
            {1, &IAudioDevice::SetAudioDeviceOutputVolume, "SetAudioDeviceOutputVolume"},
            {2, nullptr, "GetAudioDeviceOutputVolume"},
            {3, &IAudioDevice::GetActiveAudioDeviceName, "GetActiveAudioDeviceName"},
            {4, &IAudioDevice::QueryAudioDeviceSystemEvent, "QueryAudioDeviceSystemEvent"},
            {5, &IAudioDevice::GetActiveChannelCount, "GetActiveChannelCount"},
            {6, &IAudioDevice::ListAudioDeviceName,
             "ListAudioDeviceNameAuto"}, // TODO(ogniK): Confirm if autos are identical to non auto
            {7, &IAudioDevice::SetAudioDeviceOutputVolume, "SetAudioDeviceOutputVolumeAuto"},
            {8, nullptr, "GetAudioDeviceOutputVolumeAuto"},
            {10, &IAudioDevice::GetActiveAudioDeviceName, "GetActiveAudioDeviceNameAuto"},
            {11, nullptr, "QueryAudioDeviceInputEvent"},
            {12, nullptr, "QueryAudioDeviceOutputEvent"},
            {13, nullptr, "GetAudioSystemMasterVolumeSetting"},
        };
        RegisterHandlers(functions);

        auto& kernel = Core::System::GetInstance().Kernel();
        buffer_event = Kernel::WritableEvent::CreateEventPair(kernel, Kernel::ResetType::OneShot,
                                                              "IAudioOutBufferReleasedEvent");
    }

private:
    void ListAudioDeviceName(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_Audio, "(STUBBED) called");
        IPC::RequestParser rp{ctx};

        constexpr std::array<char, 15> audio_interface{{"AudioInterface"}};
        ctx.WriteBuffer(audio_interface);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(1);
    }

    void SetAudioDeviceOutputVolume(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_Audio, "(STUBBED) called");

        IPC::RequestParser rp{ctx};
        f32 volume = static_cast<f32>(rp.Pop<u32>());

        auto file_buffer = ctx.ReadBuffer();
        auto end = std::find(file_buffer.begin(), file_buffer.end(), '\0');

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void GetActiveAudioDeviceName(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_Audio, "(STUBBED) called");
        IPC::RequestParser rp{ctx};

        constexpr std::array<char, 12> audio_interface{{"AudioDevice"}};
        ctx.WriteBuffer(audio_interface);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(1);
    }

    void QueryAudioDeviceSystemEvent(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_Audio, "(STUBBED) called");

        buffer_event.writable->Signal();

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushCopyObjects(buffer_event.readable);
    }

    void GetActiveChannelCount(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_Audio, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(1);
    }

    Kernel::EventPair buffer_event;

}; // namespace Audio

AudRenU::AudRenU() : ServiceFramework("audren:u") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &AudRenU::OpenAudioRenderer, "OpenAudioRenderer"},
        {1, &AudRenU::GetAudioRendererWorkBufferSize, "GetAudioRendererWorkBufferSize"},
        {2, &AudRenU::GetAudioDeviceService, "GetAudioDeviceService"},
        {3, nullptr, "OpenAudioRendererAuto"},
        {4, &AudRenU::GetAudioDeviceServiceWithRevisionInfo, "GetAudioDeviceServiceWithRevisionInfo"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

AudRenU::~AudRenU() = default;

void AudRenU::OpenAudioRenderer(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_Audio, "called");

    IPC::RequestParser rp{ctx};
    auto params = rp.PopRaw<AudioCore::AudioRendererParameter>();
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};

    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<Audio::IAudioRenderer>(std::move(params));
}

void AudRenU::GetAudioRendererWorkBufferSize(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    auto params = rp.PopRaw<AudioCore::AudioRendererParameter>();
    LOG_DEBUG(Service_Audio, "called");

    u64 buffer_sz = Common::AlignUp(4 * params.mix_buffer_count, 0x40);
    buffer_sz += params.unknown_c * 1024;
    buffer_sz += 0x940 * (params.unknown_c + 1);
    buffer_sz += 0x3F0 * params.voice_count;
    buffer_sz += Common::AlignUp(8 * (params.unknown_c + 1), 0x10);
    buffer_sz += Common::AlignUp(8 * params.voice_count, 0x10);
    buffer_sz +=
        Common::AlignUp((0x3C0 * (params.sink_count + params.unknown_c) + 4 * params.sample_count) *
                            (params.mix_buffer_count + 6),
                        0x40);

    if (IsFeatureSupported(AudioFeatures::Splitter, params.revision)) {
        u32 count = params.unknown_c + 1;
        u64 node_count = Common::AlignUp(count, 0x40);
        u64 node_state_buffer_sz =
            4 * (node_count * node_count) + 0xC * node_count + 2 * (node_count / 8);
        u64 edge_matrix_buffer_sz = 0;
        node_count = Common::AlignUp(count * count, 0x40);
        if (node_count >> 31 != 0) {
            edge_matrix_buffer_sz = (node_count | 7) / 8;
        } else {
            edge_matrix_buffer_sz = node_count / 8;
        }
        buffer_sz += Common::AlignUp(node_state_buffer_sz + edge_matrix_buffer_sz, 0x10);
    }

    buffer_sz += 0x20 * (params.effect_count + 4 * params.voice_count) + 0x50;
    if (IsFeatureSupported(AudioFeatures::Splitter, params.revision)) {
        buffer_sz += 0xE0 * params.unknown_2c;
        buffer_sz += 0x20 * params.splitter_count;
        buffer_sz += Common::AlignUp(4 * params.unknown_2c, 0x10);
    }
    buffer_sz = Common::AlignUp(buffer_sz, 0x40) + 0x170 * params.sink_count;
    u64 output_sz = buffer_sz + 0x280 * params.sink_count + 0x4B0 * params.effect_count +
                    ((params.voice_count * 256) | 0x40);

    if (params.unknown_1c >= 1) {
        output_sz = Common::AlignUp(((16 * params.sink_count + 16 * params.effect_count +
                                      16 * params.voice_count + 16) +
                                     0x658) *
                                            (params.unknown_1c + 1) +
                                        0xc0,
                                    0x40) +
                    output_sz;
    }
    output_sz = Common::AlignUp(output_sz + 0x1807e, 0x1000);

    IPC::ResponseBuilder rb{ctx, 4};

    rb.Push(RESULT_SUCCESS);
    rb.Push<u64>(output_sz);

    LOG_DEBUG(Service_Audio, "buffer_size=0x{:X}", output_sz);
}

void AudRenU::GetAudioDeviceService(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_Audio, "called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};

    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<Audio::IAudioDevice>();
}

void AudRenU::GetAudioDeviceServiceWithRevisionInfo(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_Audio, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};

    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<Audio::IAudioDevice>(); // TODO(ogniK): Figure out what is different
                                                // based on the current revision
}

bool AudRenU::IsFeatureSupported(AudioFeatures feature, u32_le revision) const {
    u32_be version_num = (revision - Common::MakeMagic('R', 'E', 'V', '0')); // Byte swap
    switch (feature) {
    case AudioFeatures::Splitter:
        return version_num >= 2u;
    default:
        return false;
    }
}

} // namespace Service::Audio
