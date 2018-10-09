// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <memory>
#include <vector>

#include "audio_core/stream.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/swap.h"
#include "core/hle/kernel/object.h"

namespace Kernel {
class Event;
}

namespace AudioCore {

class AudioOut;

enum class PlayState : u8 {
    Started = 0,
    Stopped = 1,
    Paused = 2,
};

enum class Effect : u8 {
    None = 0,
    Aux = 2,
};

enum class EffectStatus : u8 {
    None = 0,
    New = 1,
};

struct AudioRendererParameter {
    u32_le sample_rate;
    u32_le sample_count;
    u32_le mix_buffer_count;
    u32_le unknown_c;
    u32_le voice_count;
    u32_le sink_count;
    u32_le effect_count;
    u32_le unknown_1c;
    u8 unknown_20;
    INSERT_PADDING_BYTES(3);
    u32_le splitter_count;
    u32_le unknown_2c;
    INSERT_PADDING_WORDS(1);
    u32_le revision;
};
static_assert(sizeof(AudioRendererParameter) == 52, "AudioRendererParameter is an invalid size");

enum class MemoryPoolStates : u32 { // Should be LE
    Invalid = 0x0,
    Unknown = 0x1,
    RequestDetach = 0x2,
    Detached = 0x3,
    RequestAttach = 0x4,
    Attached = 0x5,
    Released = 0x6,
};

struct MemoryPoolEntry {
    MemoryPoolStates state;
    u32_le unknown_4;
    u32_le unknown_8;
    u32_le unknown_c;
};
static_assert(sizeof(MemoryPoolEntry) == 0x10, "MemoryPoolEntry has wrong size");

struct MemoryPoolInfo {
    u64_le pool_address;
    u64_le pool_size;
    MemoryPoolStates pool_state;
    INSERT_PADDING_WORDS(3); // Unknown
};
static_assert(sizeof(MemoryPoolInfo) == 0x20, "MemoryPoolInfo has wrong size");
struct BiquadFilter {
    u8 enable;
    INSERT_PADDING_BYTES(1);
    std::array<s16_le, 3> numerator;
    std::array<s16_le, 2> denominator;
};
static_assert(sizeof(BiquadFilter) == 0xc, "BiquadFilter has wrong size");

struct WaveBuffer {
    u64_le buffer_addr;
    u64_le buffer_sz;
    s32_le start_sample_offset;
    s32_le end_sample_offset;
    u8 is_looping;
    u8 end_of_stream;
    u8 sent_to_server;
    INSERT_PADDING_BYTES(5);
    u64 context_addr;
    u64 context_sz;
    INSERT_PADDING_BYTES(8);
};
static_assert(sizeof(WaveBuffer) == 0x38, "WaveBuffer has wrong size");

struct VoiceInfo {
    u32_le id;
    u32_le node_id;
    u8 is_new;
    u8 is_in_use;
    PlayState play_state;
    u8 sample_format;
    u32_le sample_rate;
    u32_le priority;
    u32_le sorting_order;
    u32_le channel_count;
    float_le pitch;
    float_le volume;
    std::array<BiquadFilter, 2> biquad_filter;
    u32_le wave_buffer_count;
    u32_le wave_buffer_head;
    INSERT_PADDING_WORDS(1);
    u64_le additional_params_addr;
    u64_le additional_params_sz;
    u32_le mix_id;
    u32_le splitter_info_id;
    std::array<WaveBuffer, 4> wave_buffer;
    std::array<u32_le, 6> voice_channel_resource_ids;
    INSERT_PADDING_BYTES(24);
};
static_assert(sizeof(VoiceInfo) == 0x170, "VoiceInfo is wrong size");

struct VoiceOutStatus {
    u64_le played_sample_count;
    u32_le wave_buffer_consumed;
    u32_le voice_drops_count;
};
static_assert(sizeof(VoiceOutStatus) == 0x10, "VoiceOutStatus has wrong size");

struct AuxInfo {
    std::array<u8, 24> input_mix_buffers;
    std::array<u8, 24> output_mix_buffers;
    u32_le mix_buffer_count;
    u32_le sample_rate; // Stored in the aux buffer currently
    u32_le sampe_count;
    u64_le send_buffer_info;
    u64_le send_buffer_base;

    u64_le return_buffer_info;
    u64_le return_buffer_base;
};
static_assert(sizeof(AuxInfo) == 0x60, "AuxInfo is an invalid size");

struct EffectInStatus {
    Effect type;
    u8 is_new;
    u8 is_enabled;
    INSERT_PADDING_BYTES(1);
    u32_le mix_id;
    u64_le buffer_base;
    u64_le buffer_sz;
    s32_le priority;
    INSERT_PADDING_BYTES(4);
    union {
        std::array<u8, 0xa0> raw;
        AuxInfo aux_info;
    };
};
static_assert(sizeof(EffectInStatus) == 0xc0, "EffectInStatus is an invalid size");

struct EffectOutStatus {
    EffectStatus state;
    INSERT_PADDING_BYTES(0xf);
};
static_assert(sizeof(EffectOutStatus) == 0x10, "EffectOutStatus is an invalid size");

struct UpdateDataHeader {
    UpdateDataHeader() {}

    explicit UpdateDataHeader(const AudioRendererParameter& config) {
        revision = Common::MakeMagic('R', 'E', 'V', '4'); // 5.1.0 Revision
        behavior_size = 0xb0;
        memory_pools_size = (config.effect_count + (config.voice_count * 4)) * 0x10;
        voices_size = config.voice_count * 0x10;
        voice_resource_size = 0x0;
        effects_size = config.effect_count * 0x10;
        mixes_size = 0x0;
        sinks_size = config.sink_count * 0x20;
        performance_manager_size = 0x10;
        total_size = sizeof(UpdateDataHeader) + behavior_size + memory_pools_size + voices_size +
                     effects_size + sinks_size + performance_manager_size;
    }

    u32_le revision;
    u32_le behavior_size;
    u32_le memory_pools_size;
    u32_le voices_size;
    u32_le voice_resource_size;
    u32_le effects_size;
    u32_le mixes_size;
    u32_le sinks_size;
    u32_le performance_manager_size;
    INSERT_PADDING_WORDS(6);
    u32_le total_size;
};
static_assert(sizeof(UpdateDataHeader) == 0x40, "UpdateDataHeader has wrong size");

class AudioRenderer {
public:
    AudioRenderer(AudioRendererParameter params, Kernel::SharedPtr<Kernel::Event> buffer_event);
    ~AudioRenderer();

    std::vector<u8> UpdateAudioRenderer(const std::vector<u8>& input_params);
    void QueueMixedBuffer(Buffer::Tag tag);
    void ReleaseAndQueueBuffers();
    u32 GetSampleRate() const;
    u32 GetSampleCount() const;
    u32 GetMixBufferCount() const;
    Stream::State GetStreamState() const;

private:
    class EffectState;
    class VoiceState;

    AudioRendererParameter worker_params;
    Kernel::SharedPtr<Kernel::Event> buffer_event;
    std::vector<VoiceState> voices;
    std::vector<EffectState> effects;
    std::unique_ptr<AudioOut> audio_out;
    AudioCore::StreamPtr stream;
};

} // namespace AudioCore
