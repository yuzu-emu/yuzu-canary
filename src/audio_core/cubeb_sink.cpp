// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cstring>
#include "audio_core/cubeb_sink.h"
#include "audio_core/stream.h"
#include "audio_core/time_stretch.h"
#include "common/logging/log.h"
#include "common/ring_buffer.h"
#include "core/settings.h"

namespace AudioCore {

class CubebSinkStream final : public SinkStream {
public:
    CubebSinkStream(cubeb* ctx, u32 sample_rate, u32 num_channels_, cubeb_devid output_device,
                    const std::string& name)
        : ctx{ctx}, is_6_channel{num_channels_ == 6}, num_channels{std::min(num_channels_, 2u)},
          time_stretch{sample_rate, num_channels} {

        cubeb_stream_params params{};
        params.rate = sample_rate;
        params.channels = num_channels;
        params.format = CUBEB_SAMPLE_S16NE;
        params.layout = num_channels == 1 ? CUBEB_LAYOUT_MONO : CUBEB_LAYOUT_STEREO;

        u32 minimum_latency{};
        if (cubeb_get_min_latency(ctx, &params, &minimum_latency) != CUBEB_OK) {
            LOG_CRITICAL(Audio_Sink, "Error getting minimum latency");
        }

        if (cubeb_stream_init(ctx, &stream_backend, name.c_str(), nullptr, nullptr, output_device,
                              &params, std::max(512u, minimum_latency),
                              &CubebSinkStream::DataCallback, &CubebSinkStream::StateCallback,
                              this) != CUBEB_OK) {
            LOG_CRITICAL(Audio_Sink, "Error initializing cubeb stream");
            return;
        }

        if (cubeb_stream_start(stream_backend) != CUBEB_OK) {
            LOG_CRITICAL(Audio_Sink, "Error starting cubeb stream");
            return;
        }
    }

    ~CubebSinkStream() {
        if (!ctx) {
            return;
        }

        if (cubeb_stream_stop(stream_backend) != CUBEB_OK) {
            LOG_CRITICAL(Audio_Sink, "Error stopping cubeb stream");
        }

        cubeb_stream_destroy(stream_backend);
    }

    void EnqueueSamples(u32 num_channels, const std::vector<s16>& samples) override {
        if (is_6_channel) {
            // Downsample 6 channels to 2
            const size_t sample_count_copy_size = samples.size() * 2;
            std::vector<s16> buf;
            buf.reserve(sample_count_copy_size);
            for (size_t i = 0; i < samples.size(); i += num_channels) {
                buf.push_back(samples[i]);
                buf.push_back(samples[i + 1]);
            }
            queue.Push(buf);
            return;
        }

        queue.Push(samples);
    }

    size_t SamplesInQueue(u32 num_channels) const override {
        if (!ctx)
            return 0;

        return queue.Size() / num_channels;
    }

    u32 GetNumChannels() const {
        return num_channels;
    }

private:
    std::vector<std::string> device_list;

    cubeb* ctx{};
    cubeb_stream* stream_backend{};
    u32 num_channels{};
    bool is_6_channel{};

    Common::RingBuffer<s16, 0x10000> queue;
    std::array<s16, 2> last_frame;
    TimeStretcher time_stretch;

    static long DataCallback(cubeb_stream* stream, void* user_data, const void* input_buffer,
                             void* output_buffer, long num_frames);
    static void StateCallback(cubeb_stream* stream, void* user_data, cubeb_state state);
};

CubebSink::CubebSink(std::string target_device_name) {
    if (cubeb_init(&ctx, "yuzu", nullptr) != CUBEB_OK) {
        LOG_CRITICAL(Audio_Sink, "cubeb_init failed");
        return;
    }

    if (target_device_name != auto_device_name && !target_device_name.empty()) {
        cubeb_device_collection collection;
        if (cubeb_enumerate_devices(ctx, CUBEB_DEVICE_TYPE_OUTPUT, &collection) != CUBEB_OK) {
            LOG_WARNING(Audio_Sink, "Audio output device enumeration not supported");
        } else {
            const auto collection_end{collection.device + collection.count};
            const auto device{std::find_if(collection.device, collection_end,
                                           [&](const cubeb_device_info& device) {
                                               return target_device_name == device.friendly_name;
                                           })};
            if (device != collection_end) {
                output_device = device->devid;
            }
            cubeb_device_collection_destroy(ctx, &collection);
        }
    }
}

CubebSink::~CubebSink() {
    if (!ctx) {
        return;
    }

    for (auto& sink_stream : sink_streams) {
        sink_stream.reset();
    }

    cubeb_destroy(ctx);
}

SinkStream& CubebSink::AcquireSinkStream(u32 sample_rate, u32 num_channels,
                                         const std::string& name) {
    sink_streams.push_back(
        std::make_unique<CubebSinkStream>(ctx, sample_rate, num_channels, output_device, name));
    return *sink_streams.back();
}

long CubebSinkStream::DataCallback(cubeb_stream* stream, void* user_data, const void* input_buffer,
                                   void* output_buffer, long num_frames) {
    CubebSinkStream* impl = static_cast<CubebSinkStream*>(user_data);
    u8* buffer = reinterpret_cast<u8*>(output_buffer);

    if (!impl) {
        return {};
    }

    const size_t num_channels = impl->GetNumChannels();
    const size_t samples_to_write = num_channels * num_frames;
    size_t samples_written;

    if (Settings::values.enable_audio_stretching) {
        const std::vector<s16> in{impl->queue.Pop()};
        const size_t num_in{in.size() / num_channels};
        s16* const out{reinterpret_cast<s16*>(buffer)};
        const size_t out_frames = impl->time_stretch.Process(in.data(), num_in, out, num_frames);
        samples_written = out_frames * num_channels;
    } else {
        samples_written = impl->queue.Pop(buffer, samples_to_write);
    }

    if (samples_written >= num_channels) {
        std::memcpy(&impl->last_frame[0], buffer + (samples_written - num_channels) * sizeof(s16),
                    num_channels * sizeof(s16));
    }

    // Fill the rest of the frames with last_frame
    for (size_t i = samples_written; i < samples_to_write; i += num_channels) {
        std::memcpy(buffer + i * sizeof(s16), &impl->last_frame[0], num_channels * sizeof(s16));
    }

    return num_frames;
}

void CubebSinkStream::StateCallback(cubeb_stream* stream, void* user_data, cubeb_state state) {}

std::vector<std::string> ListCubebSinkDevices() {
    std::vector<std::string> device_list;
    cubeb* ctx;

    if (cubeb_init(&ctx, "Citra Device Enumerator", nullptr) != CUBEB_OK) {
        LOG_CRITICAL(Audio_Sink, "cubeb_init failed");
        return {};
    }

    cubeb_device_collection collection;
    if (cubeb_enumerate_devices(ctx, CUBEB_DEVICE_TYPE_OUTPUT, &collection) != CUBEB_OK) {
        LOG_WARNING(Audio_Sink, "Audio output device enumeration not supported");
    } else {
        for (size_t i = 0; i < collection.count; i++) {
            const cubeb_device_info& device = collection.device[i];
            if (device.friendly_name) {
                device_list.emplace_back(device.friendly_name);
            }
        }
        cubeb_device_collection_destroy(ctx, &collection);
    }

    cubeb_destroy(ctx);
    return device_list;
}

} // namespace AudioCore
