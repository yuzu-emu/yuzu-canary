// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "audio_core/audio_renderer.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "core/memory.h"

namespace AudioCore {

constexpr u32 STREAM_SAMPLE_RATE{48000};
constexpr u32 STREAM_NUM_CHANNELS{2};

AudioRenderer::AudioRenderer(AudioRendererParameter params,
                             Kernel::SharedPtr<Kernel::Event> buffer_event)
    : worker_params{params}, buffer_event{buffer_event}, voices(params.voice_count) {

    audio_core = std::make_unique<AudioCore::AudioOut>();
    stream = audio_core->OpenStream(STREAM_SAMPLE_RATE, STREAM_NUM_CHANNELS, "AudioRenderer",
                                    [=]() { buffer_event->Signal(); });
    audio_core->StartStream(stream);

    QueueMixedBuffer(0);
    QueueMixedBuffer(1);
    QueueMixedBuffer(2);
}

std::vector<u8> AudioRenderer::UpdateAudioRenderer(const std::vector<u8>& input_params) {
    // Copy UpdateDataHeader struct
    UpdateDataHeader config{};
    std::memcpy(&config, input_params.data(), sizeof(UpdateDataHeader));
    u32 memory_pool_count = worker_params.effect_count + (worker_params.voice_count * 4);

    // Copy MemoryPoolInfo structs
    std::vector<MemoryPoolInfo> mem_pool_info(memory_pool_count);
    std::memcpy(mem_pool_info.data(),
                input_params.data() + sizeof(UpdateDataHeader) + config.behavior_size,
                memory_pool_count * sizeof(MemoryPoolInfo));

    // Copy VoiceInfo structs
    size_t offset{sizeof(UpdateDataHeader) + config.behavior_size + config.memory_pools_size +
                  config.voice_resource_size};
    for (auto& voice : voices) {
        std::memcpy(&voice.Info(), input_params.data() + offset, sizeof(VoiceInfo));
        offset += sizeof(VoiceInfo);
    }

    // Update voices
    for (auto& voice : voices) {
        voice.UpdateState();
        if (!voice.GetInfo().is_in_use) {
            continue;
        }
        if (voice.GetInfo().is_new) {
            voice.SetWaveIndex(voice.GetInfo().wave_buffer_head);
        }
    }

    // Update memory pool state
    std::vector<MemoryPoolEntry> memory_pool(memory_pool_count);
    for (size_t index = 0; index < memory_pool.size(); ++index) {
        if (mem_pool_info[index].pool_state == MemoryPoolStates::RequestAttach) {
            memory_pool[index].state = MemoryPoolStates::Attached;
        } else if (mem_pool_info[index].pool_state == MemoryPoolStates::RequestDetach) {
            memory_pool[index].state = MemoryPoolStates::Detached;
        }
    }

    // Release previous buffers and queue next ones for playback
    ReleaseAndQueueBuffers();

    // Copy output header
    UpdateDataHeader response_data{worker_params};
    std::vector<u8> output_params(response_data.total_size);
    std::memcpy(output_params.data(), &response_data, sizeof(UpdateDataHeader));

    // Copy output memory pool entries
    std::memcpy(output_params.data() + sizeof(UpdateDataHeader), memory_pool.data(),
                response_data.memory_pools_size);

    // Copy output voice status
    size_t voice_out_status_offset{sizeof(UpdateDataHeader) + response_data.memory_pools_size};
    for (const auto& voice : voices) {
        std::memcpy(output_params.data() + voice_out_status_offset, &voice.GetOutStatus(),
                    sizeof(VoiceOutStatus));
        voice_out_status_offset += sizeof(VoiceOutStatus);
    }

    return output_params;
}

void AudioRenderer::VoiceState::SetWaveIndex(size_t index) {
    wave_index = index & 3;
    is_refresh_pending = true;
}

std::vector<s16> AudioRenderer::VoiceState::DequeueSamples(size_t sample_count) {
    if (!IsPlaying()) {
        return {};
    }

    if (is_refresh_pending) {
        RefreshBuffer();
    }

    const size_t max_size{samples.size() - offset};
    const size_t dequeue_offset{offset};
    size_t size{sample_count * STREAM_NUM_CHANNELS};
    if (size > max_size) {
        size = max_size;
    }

    out_status.played_sample_count += size / STREAM_NUM_CHANNELS;
    offset += size;

    const auto& wave_buffer{info.wave_buffer[wave_index]};
    if (offset == samples.size()) {
        offset = 0;

        if (!wave_buffer.is_looping) {
            SetWaveIndex(wave_index + 1);
        }

        out_status.wave_buffer_consumed++;

        if (wave_buffer.end_of_stream) {
            info.play_state = PlayState::Paused;
        }
    }

    return {samples.begin() + dequeue_offset, samples.begin() + dequeue_offset + size};
}

void AudioRenderer::VoiceState::UpdateState() {
    if (is_in_use && !info.is_in_use) {
        // No longer in use, reset state
        is_refresh_pending = true;
        wave_index = 0;
        offset = 0;
        out_status = {};
    }
    is_in_use = info.is_in_use;
}

void AudioRenderer::VoiceState::RefreshBuffer() {
    std::vector<s16> new_samples(info.wave_buffer[wave_index].buffer_sz / sizeof(s16));
    Memory::ReadBlock(info.wave_buffer[wave_index].buffer_addr, new_samples.data(),
                      info.wave_buffer[wave_index].buffer_sz);

    switch (static_cast<Codec::PcmFormat>(info.sample_format)) {
    case Codec::PcmFormat::Int16: {
        // PCM16 is played as-is
        break;
    }
    case Codec::PcmFormat::Adpcm: {
        // Decode ADPCM to PCM16
        Codec::ADPCM_Coeff coeffs;
        Memory::ReadBlock(info.additional_params_addr, coeffs.data(), sizeof(Codec::ADPCM_Coeff));
        new_samples = Codec::DecodeADPCM(reinterpret_cast<u8*>(new_samples.data()),
                                         new_samples.size() * sizeof(s16), coeffs, adpcm_state);
        break;
    }
    default:
        LOG_CRITICAL(Audio, "Unimplemented sample_format={}", info.sample_format);
        UNREACHABLE();
        break;
    }

    switch (info.channel_count) {
    case 1:
        // 1 channel is upsampled to 2 channel
        samples.resize(new_samples.size() * 2);
        for (size_t index = 0; index < new_samples.size(); ++index) {
            samples[index * 2] = new_samples[index];
            samples[index * 2 + 1] = new_samples[index];
        }
        break;
    case 2: {
        // 2 channel is played as is
        samples = std::move(new_samples);
        break;
    }
    default:
        LOG_CRITICAL(Audio, "Unimplemented channel_count={}", info.channel_count);
        UNREACHABLE();
        break;
    }

    is_refresh_pending = false;
}

static constexpr s16 ClampToS16(s32 value) {
    return static_cast<s16>(std::clamp(value, -32768, 32767));
}

void AudioRenderer::QueueMixedBuffer(Buffer::Tag tag) {
    constexpr size_t BUFFER_SIZE{512};
    std::vector<s16> buffer(BUFFER_SIZE * stream->GetNumChannels());

    for (auto& voice : voices) {
        if (!voice.IsPlaying()) {
            continue;
        }

        size_t offset{};
        s64 samples_remaining{BUFFER_SIZE};
        while (samples_remaining > 0) {
            const std::vector<s16> samples{voice.DequeueSamples(samples_remaining)};

            if (samples.empty()) {
                break;
            }

            samples_remaining -= samples.size();

            for (const auto& sample : samples) {
                const s32 buffer_sample{buffer[offset]};
                buffer[offset++] =
                    ClampToS16(buffer_sample + static_cast<s32>(sample * voice.GetInfo().volume));
            }
        }
    }
    audio_core->QueueBuffer(stream, tag, std::move(buffer));
}

void AudioRenderer::ReleaseAndQueueBuffers() {
    const auto released_buffers{audio_core->GetTagsAndReleaseBuffers(stream, 2)};
    for (const auto& tag : released_buffers) {
        QueueMixedBuffer(tag);
    }
}

} // namespace AudioCore
