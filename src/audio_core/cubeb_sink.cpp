// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cstring>

#include "audio_core/cubeb_sink.h"
#include "audio_core/stream.h"
#include "common/logging/log.h"

namespace AudioCore {

class SinkStreamImpl final : public SinkStream {
public:
    SinkStreamImpl(cubeb* ctx, u32 sample_rate, u32 num_channels_, cubeb_devid output_device,
                   const std::string& name)
        : ctx{ctx}, num_channels{num_channels_} {

        if (num_channels == 6) {
            // 6-channel audio does not seem to work with cubeb + SDL, so we downsample this to 2
            // channel for now
            is_6_channel = true;
            num_channels = 2;
        }

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
                              &SinkStreamImpl::DataCallback, &SinkStreamImpl::StateCallback,
                              this) != CUBEB_OK) {
            LOG_CRITICAL(Audio_Sink, "Error initializing cubeb stream");
            return;
        }

        if (cubeb_stream_start(stream_backend) != CUBEB_OK) {
            LOG_CRITICAL(Audio_Sink, "Error starting cubeb stream");
            return;
        }
    }

    ~SinkStreamImpl() {
        if (!ctx) {
            return;
        }

        if (cubeb_stream_stop(stream_backend) != CUBEB_OK) {
            LOG_CRITICAL(Audio_Sink, "Error stopping cubeb stream");
        }

        cubeb_stream_destroy(stream_backend);
    }

    void EnqueueSamples(u32 num_channels, const std::vector<s16>& samples) override {
        if (!ctx) {
            return;
        }

        queue.reserve(queue.size() + samples.size() * GetNumChannels());

        if (is_6_channel) {
            // Downsample 6 channels to 2
            const size_t sample_count_copy_size = samples.size() * 2;
            queue.reserve(sample_count_copy_size);
            for (size_t i = 0; i < samples.size(); i += num_channels) {
                queue.push_back(samples[i]);
                queue.push_back(samples[i + 1]);
            }
        } else {
            // Copy as-is
            std::copy(samples.begin(), samples.end(), std::back_inserter(queue));
        }
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

    std::vector<s16> queue;

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
        std::make_unique<SinkStreamImpl>(ctx, sample_rate, num_channels, output_device, name));
    return *sink_streams.back();
}

long SinkStreamImpl::DataCallback(cubeb_stream* stream, void* user_data, const void* input_buffer,
                                  void* output_buffer, long num_frames) {
    SinkStreamImpl* impl = static_cast<SinkStreamImpl*>(user_data);
    u8* buffer = reinterpret_cast<u8*>(output_buffer);

    if (!impl) {
        return {};
    }

    const size_t frames_to_write{
        std::min(impl->queue.size() / impl->GetNumChannels(), static_cast<size_t>(num_frames))};

    memcpy(buffer, impl->queue.data(), frames_to_write * sizeof(s16) * impl->GetNumChannels());
    impl->queue.erase(impl->queue.begin(),
                      impl->queue.begin() + frames_to_write * impl->GetNumChannels());

    if (frames_to_write < num_frames) {
        // Fill the rest of the frames with silence
        memset(buffer + frames_to_write * sizeof(s16) * impl->GetNumChannels(), 0,
               (num_frames - frames_to_write) * sizeof(s16) * impl->GetNumChannels());
    }

    return num_frames;
}

void SinkStreamImpl::StateCallback(cubeb_stream* stream, void* user_data, cubeb_state state) {}

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
