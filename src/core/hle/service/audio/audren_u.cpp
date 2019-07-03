// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <memory>

#include "audio_core/audio_renderer.h"
#include "common/alignment.h"
#include "common/bit_util.h"
#include "common/common_funcs.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/hle_ipc.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/readable_event.h"
#include "core/hle/kernel/writable_event.h"
#include "core/hle/service/audio/audren_u.h"
#include "core/hle/service/audio/errors.h"

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
            {11, &IAudioRenderer::ExecuteAudioRendererRendering, "ExecuteAudioRendererRendering"},
        };
        // clang-format on
        RegisterHandlers(functions);

        auto& system = Core::System::GetInstance();
        system_event = Kernel::WritableEvent::CreateEventPair(
            system.Kernel(), Kernel::ResetType::Manual, "IAudioRenderer:SystemEvent");
        renderer = std::make_unique<AudioCore::AudioRenderer>(system.CoreTiming(), audren_params,
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

    void ExecuteAudioRendererRendering(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_Audio, "called");

        // This service command currently only reports an unsupported operation
        // error code, or aborts. Given that, we just always return an error
        // code in this case.

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ERR_NOT_SUPPORTED);
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
            {6, &IAudioDevice::ListAudioDeviceName, "ListAudioDeviceNameAuto"},
            {7, &IAudioDevice::SetAudioDeviceOutputVolume, "SetAudioDeviceOutputVolumeAuto"},
            {8, nullptr, "GetAudioDeviceOutputVolumeAuto"},
            {10, &IAudioDevice::GetActiveAudioDeviceName, "GetActiveAudioDeviceNameAuto"},
            {11, nullptr, "QueryAudioDeviceInputEvent"},
            {12, &IAudioDevice::QueryAudioDeviceOutputEvent, "QueryAudioDeviceOutputEvent"},
            {13, nullptr, "GetAudioSystemMasterVolumeSetting"},
        };
        RegisterHandlers(functions);

        auto& kernel = Core::System::GetInstance().Kernel();
        buffer_event = Kernel::WritableEvent::CreateEventPair(kernel, Kernel::ResetType::Automatic,
                                                              "IAudioOutBufferReleasedEvent");

        // Should only be signalled when an audio output device has been changed, example: speaker
        // to headset
        audio_output_device_switch_event = Kernel::WritableEvent::CreateEventPair(
            kernel, Kernel::ResetType::Automatic, "IAudioDevice:AudioOutputDeviceSwitchedEvent");
    }

private:
    void ListAudioDeviceName(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_Audio, "(STUBBED) called");

        constexpr std::array<char, 15> audio_interface{{"AudioInterface"}};
        ctx.WriteBuffer(audio_interface);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(1);
    }

    void SetAudioDeviceOutputVolume(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const f32 volume = rp.Pop<f32>();

        const auto device_name_buffer = ctx.ReadBuffer();
        const std::string name = Common::StringFromBuffer(device_name_buffer);

        LOG_WARNING(Service_Audio, "(STUBBED) called. name={}, volume={}", name, volume);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void GetActiveAudioDeviceName(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_Audio, "(STUBBED) called");

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

    void QueryAudioDeviceOutputEvent(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_Audio, "called");

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushCopyObjects(audio_output_device_switch_event.readable);
    }

    Kernel::EventPair buffer_event;
    Kernel::EventPair audio_output_device_switch_event;

}; // namespace Audio

AudRenU::AudRenU() : ServiceFramework("audren:u") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &AudRenU::OpenAudioRenderer, "OpenAudioRenderer"},
        {1, &AudRenU::GetAudioRendererWorkBufferSize, "GetAudioRendererWorkBufferSize"},
        {2, &AudRenU::GetAudioDeviceService, "GetAudioDeviceService"},
        {3, &AudRenU::OpenAudioRendererAuto, "OpenAudioRendererAuto"},
        {4, &AudRenU::GetAudioDeviceServiceWithRevisionInfo, "GetAudioDeviceServiceWithRevisionInfo"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

AudRenU::~AudRenU() = default;

void AudRenU::OpenAudioRenderer(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_Audio, "called");

    OpenAudioRendererImpl(ctx);
}

static u64 CalculateNumPerformanceEntries(const AudioCore::AudioRendererParameter& params) {
    // +1 represents the final mix.
    return u64{params.effect_count} + params.submix_count + params.sink_count + params.voice_count +
           1;
}

void AudRenU::GetAudioRendererWorkBufferSize(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_Audio, "called");

    // Several calculations below align the sizes being calculated
    // onto a 64 byte boundary.
    static constexpr u64 buffer_alignment_size = 64;

    // Some calculations that calculate portions of the buffer
    // that will contain information, on the other hand, align
    // the result of some of their calcularions on a 16 byte boundary.
    static constexpr u64 info_field_alignment_size = 16;

    // Maximum detail entries that may exist at one time for performance
    // frame statistics.
    static constexpr u64 max_perf_detail_entries = 100;

    // Size of the data structure representing the bulk of the voice-related state.
    static constexpr u64 voice_state_size = 0x100;

    // Size of the upsampler manager data structure
    constexpr u64 upsampler_manager_size = 0x48;

    // Calculates the part of the size that relates to mix buffers.
    const auto calculate_mix_buffer_sizes = [](const AudioCore::AudioRendererParameter& params) {
        // As of 8.0.0 this is the maximum on voice channels.
        constexpr u64 max_voice_channels = 6;

        // The service expects the sample_count member of the parameters to either be
        // a value of 160 or 240, so the maximum sample count is assumed in order
        // to adequately handle all values at runtime.
        constexpr u64 default_max_sample_count = 240;

        const u64 total_mix_buffers = params.mix_buffer_count + max_voice_channels;

        u64 size = 0;
        size += total_mix_buffers * (sizeof(s32) * params.sample_count);
        size += total_mix_buffers * (sizeof(s32) * default_max_sample_count);
        size += u64{params.submix_count} + params.sink_count;
        size = Common::AlignUp(size, buffer_alignment_size);
        size += Common::AlignUp(params.unknown_30, buffer_alignment_size);
        size += Common::AlignUp(sizeof(s32) * params.mix_buffer_count, buffer_alignment_size);
        return size;
    };

    // Calculates the portion of the size related to the mix data (and the sorting thereof).
    const auto calculate_mix_info_size = [this](const AudioCore::AudioRendererParameter& params) {
        // The size of the mixing info data structure.
        constexpr u64 mix_info_size = 0x940;

        // Consists of total submixes with the final mix included.
        const u64 total_mix_count = u64{params.submix_count} + 1;

        // The total number of effects that may be available to the audio renderer at any time.
        constexpr u64 max_effects = 256;

        // Calculates the part of the size related to the audio node state.
        // This will only be used if the audio revision supports the splitter.
        const auto calculate_node_state_size = [](std::size_t num_nodes) {
            // Internally within a nodestate, it appears to use a data structure
            // similar to a std::bitset<64> twice.
            constexpr u64 bit_size = Common::BitSize<u64>();
            constexpr u64 num_bitsets = 2;

            // Node state instances have three states internally for performing
            // depth-first searches of nodes. Initialized, Found, and Done Sorting.
            constexpr u64 num_states = 3;

            u64 size = 0;
            size += (num_nodes * num_nodes) * sizeof(s32);
            size += num_states * (num_nodes * sizeof(s32));
            size += num_bitsets * (Common::AlignUp(num_nodes, bit_size) / Common::BitSize<u8>());
            return size;
        };

        // Calculates the part of the size related to the adjacency (aka edge) matrix.
        const auto calculate_edge_matrix_size = [](std::size_t num_nodes) {
            return (num_nodes * num_nodes) * sizeof(s32);
        };

        u64 size = 0;
        size += Common::AlignUp(sizeof(void*) * total_mix_count, info_field_alignment_size);
        size += Common::AlignUp(mix_info_size * total_mix_count, info_field_alignment_size);
        size += Common::AlignUp(sizeof(s32) * max_effects * params.submix_count,
                                info_field_alignment_size);

        if (IsFeatureSupported(AudioFeatures::Splitter, params.revision)) {
            size += Common::AlignUp(calculate_node_state_size(total_mix_count) +
                                        calculate_edge_matrix_size(total_mix_count),
                                    info_field_alignment_size);
        }

        return size;
    };

    // Calculates the part of the size related to voice channel info.
    const auto calculate_voice_info_size = [](const AudioCore::AudioRendererParameter& params) {
        constexpr u64 voice_info_size = 0x220;
        constexpr u64 voice_resource_size = 0xD0;

        u64 size = 0;
        size += Common::AlignUp(sizeof(void*) * params.voice_count, info_field_alignment_size);
        size += Common::AlignUp(voice_info_size * params.voice_count, info_field_alignment_size);
        size +=
            Common::AlignUp(voice_resource_size * params.voice_count, info_field_alignment_size);
        size += Common::AlignUp(voice_state_size * params.voice_count, info_field_alignment_size);
        return size;
    };

    // Calculates the part of the size related to memory pools.
    const auto calculate_memory_pools_size = [](const AudioCore::AudioRendererParameter& params) {
        const u64 num_memory_pools = sizeof(s32) * (u64{params.effect_count} + params.voice_count);
        const u64 memory_pool_info_size = 0x20;
        return Common::AlignUp(num_memory_pools * memory_pool_info_size, info_field_alignment_size);
    };

    // Calculates the part of the size related to the splitter context.
    const auto calculate_splitter_context_size =
        [this](const AudioCore::AudioRendererParameter& params) -> u64 {
        if (!IsFeatureSupported(AudioFeatures::Splitter, params.revision)) {
            return 0;
        }

        constexpr u64 splitter_info_size = 0x20;
        constexpr u64 splitter_destination_data_size = 0xE0;

        u64 size = 0;
        size += params.num_splitter_send_channels;
        size +=
            Common::AlignUp(splitter_info_size * params.splitter_count, info_field_alignment_size);
        size += Common::AlignUp(splitter_destination_data_size * params.num_splitter_send_channels,
                                info_field_alignment_size);

        return size;
    };

    // Calculates the part of the size related to the upsampler info.
    const auto calculate_upsampler_info_size = [](const AudioCore::AudioRendererParameter& params) {
        constexpr u64 upsampler_info_size = 0x280;
        // Yes, using the buffer size over info alignment size is intentional here.
        return Common::AlignUp(upsampler_info_size * (u64{params.submix_count} + params.sink_count),
                               buffer_alignment_size);
    };

    // Calculates the part of the size related to effect info.
    const auto calculate_effect_info_size = [](const AudioCore::AudioRendererParameter& params) {
        constexpr u64 effect_info_size = 0x2B0;
        return Common::AlignUp(effect_info_size * params.effect_count, info_field_alignment_size);
    };

    // Calculates the part of the size related to audio sink info.
    const auto calculate_sink_info_size = [](const AudioCore::AudioRendererParameter& params) {
        const u64 sink_info_size = 0x170;
        return Common::AlignUp(sink_info_size * params.sink_count, info_field_alignment_size);
    };

    // Calculates the part of the size related to voice state info.
    const auto calculate_voice_state_size = [](const AudioCore::AudioRendererParameter& params) {
        const u64 voice_state_size = 0x100;
        const u64 additional_size = buffer_alignment_size - 1;
        return Common::AlignUp(voice_state_size * params.voice_count + additional_size,
                               info_field_alignment_size);
    };

    // Calculates the part of the size related to performance statistics.
    const auto calculate_perf_size = [this](const AudioCore::AudioRendererParameter& params) {
        // Extra size value appended to the end of the calculation.
        constexpr u64 appended = 128;

        // Whether or not we assume the newer version of performance metrics data structures.
        const bool is_v2 =
            IsFeatureSupported(AudioFeatures::PerformanceMetricsVersion2, params.revision);

        // Data structure sizes
        constexpr u64 perf_statistics_size = 0x0C;
        const u64 header_size = is_v2 ? 0x30 : 0x18;
        const u64 entry_size = is_v2 ? 0x18 : 0x10;
        const u64 detail_size = is_v2 ? 0x18 : 0x10;

        const u64 entry_count = CalculateNumPerformanceEntries(params);
        const u64 size_per_frame =
            header_size + (entry_size * entry_count) + (detail_size * max_perf_detail_entries);

        u64 size = 0;
        size += Common::AlignUp(size_per_frame * params.performance_frame_count + 1,
                                buffer_alignment_size);
        size += Common::AlignUp(perf_statistics_size, buffer_alignment_size);
        size += appended;
        return size;
    };

    // Calculates the part of the size that relates to the audio command buffer.
    const auto calculate_command_buffer_size =
        [this](const AudioCore::AudioRendererParameter& params) {
            constexpr u64 alignment = (buffer_alignment_size - 1) * 2;

            if (!IsFeatureSupported(AudioFeatures::VariadicCommandBuffer, params.revision)) {
                constexpr u64 command_buffer_size = 0x18000;

                return command_buffer_size + alignment;
            }

            // When the variadic command buffer is supported, this means
            // the command generator for the audio renderer can issue commands
            // that are (as one would expect), variable in size. So what we need to do
            // is determine the maximum possible size for a few command data structures
            // then multiply them by the amount of present commands indicated by the given
            // respective audio parameters.

            constexpr u64 max_biquad_filters = 2;
            constexpr u64 max_mix_buffers = 24;

            constexpr u64 biquad_filter_command_size = 0x2C;

            constexpr u64 depop_mix_command_size = 0x24;
            constexpr u64 depop_setup_command_size = 0x50;

            constexpr u64 effect_command_max_size = 0x540;

            constexpr u64 mix_command_size = 0x1C;
            constexpr u64 mix_ramp_command_size = 0x24;
            constexpr u64 mix_ramp_grouped_command_size = 0x13C;

            constexpr u64 perf_command_size = 0x28;

            constexpr u64 sink_command_size = 0x130;

            constexpr u64 submix_command_max_size =
                depop_mix_command_size + (mix_command_size * max_mix_buffers) * max_mix_buffers;

            constexpr u64 volume_command_size = 0x1C;
            constexpr u64 volume_ramp_command_size = 0x20;

            constexpr u64 voice_biquad_filter_command_size =
                biquad_filter_command_size * max_biquad_filters;
            constexpr u64 voice_data_command_size = 0x9C;
            const u64 voice_command_max_size =
                (params.splitter_count * depop_setup_command_size) +
                (voice_data_command_size + voice_biquad_filter_command_size +
                 volume_ramp_command_size + mix_ramp_grouped_command_size);

            // Now calculate the individual elements that comprise the size and add them together.
            const u64 effect_commands_size = params.effect_count * effect_command_max_size;

            const u64 final_mix_commands_size =
                depop_mix_command_size + volume_command_size * max_mix_buffers;

            const u64 perf_commands_size =
                perf_command_size *
                (CalculateNumPerformanceEntries(params) + max_perf_detail_entries);

            const u64 sink_commands_size = params.sink_count * sink_command_size;

            const u64 splitter_commands_size =
                params.num_splitter_send_channels * max_mix_buffers * mix_ramp_command_size;

            const u64 submix_commands_size = params.submix_count * submix_command_max_size;

            const u64 voice_commands_size = params.voice_count * voice_command_max_size;

            return effect_commands_size + final_mix_commands_size + perf_commands_size +
                   sink_commands_size + splitter_commands_size + submix_commands_size +
                   voice_commands_size + alignment;
        };

    IPC::RequestParser rp{ctx};
    const auto params = rp.PopRaw<AudioCore::AudioRendererParameter>();

    u64 size = 0;
    size += calculate_mix_buffer_sizes(params);
    size += calculate_mix_info_size(params);
    size += calculate_voice_info_size(params);
    size += upsampler_manager_size;
    size += calculate_memory_pools_size(params);
    size += calculate_splitter_context_size(params);

    size = Common::AlignUp(size, buffer_alignment_size);

    size += calculate_upsampler_info_size(params);
    size += calculate_effect_info_size(params);
    size += calculate_sink_info_size(params);
    size += calculate_voice_state_size(params);
    size += calculate_perf_size(params);
    size += calculate_command_buffer_size(params);

    // finally, 4KB page align the size, and we're done.
    size = Common::AlignUp(size, 4096);

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u64>(size);

    LOG_DEBUG(Service_Audio, "buffer_size=0x{:X}", size);
}

void AudRenU::GetAudioDeviceService(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_Audio, "called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};

    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<Audio::IAudioDevice>();
}

void AudRenU::OpenAudioRendererAuto(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_Audio, "called");

    OpenAudioRendererImpl(ctx);
}

void AudRenU::GetAudioDeviceServiceWithRevisionInfo(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_Audio, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};

    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<Audio::IAudioDevice>(); // TODO(ogniK): Figure out what is different
                                                // based on the current revision
}

void AudRenU::OpenAudioRendererImpl(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto params = rp.PopRaw<AudioCore::AudioRendererParameter>();
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};

    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<IAudioRenderer>(params);
}

bool AudRenU::IsFeatureSupported(AudioFeatures feature, u32_le revision) const {
    // Byte swap
    const u32_be version_num = revision - Common::MakeMagic('R', 'E', 'V', '0');

    switch (feature) {
    case AudioFeatures::Splitter:
        return version_num >= 2U;
    case AudioFeatures::PerformanceMetricsVersion2:
    case AudioFeatures::VariadicCommandBuffer:
        return version_num >= 5U;
    default:
        return false;
    }
}

} // namespace Service::Audio
