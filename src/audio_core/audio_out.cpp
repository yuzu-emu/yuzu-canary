// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "audio_core/audio_out.h"
#include "audio_core/sink.h"
#include "audio_core/sink_details.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "core/settings.h"

namespace AudioCore {

/// Returns the stream format from the specified number of channels
static Stream::Format ChannelsToStreamFormat(u32 num_channels) {
    switch (num_channels) {
    case 1:
        return Stream::Format::Mono16;
    case 2:
        return Stream::Format::Stereo16;
    case 6:
        return Stream::Format::Multi51Channel16;
    }

    LOG_CRITICAL(Audio, "Unimplemented num_channels={}", num_channels);
    UNREACHABLE();
    return {};
}

StreamPtr AudioOut::OpenStream(u32 sample_rate, u32 num_channels,
                               Stream::ReleaseCallback&& release_callback) {
    if (!sink) {
        const SinkDetails& sink_details = GetSinkDetails(Settings::values.sink_id);
        sink = sink_details.factory(Settings::values.audio_device_id);
    }

    return std::make_shared<Stream>(sample_rate, ChannelsToStreamFormat(num_channels),
                                    std::move(release_callback),
                                    sink->AcquireSinkStream(sample_rate, num_channels));
}

std::vector<u64> AudioOut::GetTagsAndReleaseBuffers(StreamPtr stream, size_t max_count) {
    return stream->GetTagsAndReleaseBuffers(max_count);
}

void AudioOut::StartStream(StreamPtr stream) {
    stream->Play();
}

void AudioOut::StopStream(StreamPtr stream) {
    stream->Stop();
}

bool AudioOut::QueueBuffer(StreamPtr stream, Buffer::Tag tag, std::vector<u8>&& data) {
    return stream->QueueBuffer(std::make_shared<Buffer>(tag, std::move(data)));
}

} // namespace AudioCore
