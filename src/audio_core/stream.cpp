// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/logging/log.h"
#include "core/core_timing.h"
#include "core/core_timing_util.h"

#include "audio_core/stream.h"

namespace AudioCore {

constexpr size_t MaxAudioBufferCount{32};

/// Returns the sample size for the specified audio stream format
static size_t SampleSizeFromFormat(Stream::Format format) {
    switch (format) {
    case Stream::Format::Mono16:
    case Stream::Format::Stereo16:
    case Stream::Format::Multi51Channel16:
        return 2;
    };

    LOG_CRITICAL(Audio, "Unimplemented format={}", static_cast<u32>(format));
    UNREACHABLE();
    return {};
}

Stream::Stream(int sample_rate, Format format, ReleaseCallback&& release_callback)
    : sample_rate{sample_rate}, format{format}, release_callback{std::move(release_callback)} {
    release_event = CoreTiming::RegisterEvent(
        "Stream::Release", [this](u64 userdata, int cycles_late) { ReleaseActiveBuffer(); });
}

void Stream::Play() {
    state = State::Playing;
    PlayNextBuffer();
}

void Stream::Stop() {
    ASSERT_MSG(false, "Unimplemented");
}

s64 Stream::GetBufferReleaseCycles(const Buffer& buffer) const {
    // TODO(bunnei): The below is off by a factor of 10, but it works much better with all games
    // tested. This is likely due to our wildly incorrect core timing. Needs to be fixed once core
    // timing is more accurate.
    const size_t num_samples{buffer.GetData().size() / SampleSizeFromFormat(format)};
    return CoreTiming::usToCycles((static_cast<u64>(num_samples) * 100000) / sample_rate);
}

void Stream::PlayNextBuffer() {
    if (!IsPlaying()) {
        // Ensure we are in playing state before playing the next buffer
        return;
    }

    if (active_buffer) {
        // Do not queue a new buffer if we are already playing a buffer
        return;
    }

    if (queued_buffers.empty()) {
        // No queued buffers - we are effectively paused
        return;
    }

    active_buffer = queued_buffers.front();
    queued_buffers.pop();

    CoreTiming::ScheduleEventThreadsafe(GetBufferReleaseCycles(*active_buffer), release_event, {});
}

void Stream::ReleaseActiveBuffer() {
    released_buffers.push(std::move(active_buffer));
    release_callback();
    PlayNextBuffer();
}

bool Stream::QueueBuffer(BufferPtr&& buffer) {
    if (queued_buffers.size() < MaxAudioBufferCount) {
        queued_buffers.push(std::move(buffer));
        PlayNextBuffer();
        return true;
    }
    return false;
}

bool Stream::ContainsBuffer(Buffer::Tag tag) const {
    ASSERT_MSG(false, "Unimplemented");
    return {};
}

std::vector<Buffer::Tag> Stream::GetReleasedBuffers(size_t max_count) {
    std::vector<Buffer::Tag> tags;
    for (size_t count = 0; count < max_count && !released_buffers.empty(); ++count) {
        tags.push_back(released_buffers.front()->GetTag());
        released_buffers.pop();
    }
    return tags;
}

} // namespace AudioCore
