// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <queue>

#include "audio_core/buffer.h"
#include "audio_core/sink_stream.h"
#include "common/assert.h"
#include "common/common_types.h"
#include "core/core_timing.h"

namespace AudioCore {

/**
 * Represents an audio stream, which is a sequence of queued buffers, to be outputed by AudioOut
 */
class Stream {
public:
    /// Audio format of the stream
    enum class Format {
        Mono16,
        Stereo16,
        Multi51Channel16,
    };

    /// Callback function type, used to change guest state on a buffer being released
    using ReleaseCallback = std::function<void()>;

    Stream(u32 sample_rate, Format format, ReleaseCallback&& release_callback,
           SinkStream& sink_stream, std::string&& name_);

    /// Plays the audio stream
    void Play();

    /// Stops the audio stream
    void Stop();

    /// Queues a buffer into the audio stream, returns true on success
    bool QueueBuffer(BufferPtr&& buffer);

    /// Returns true if the audio stream contains a buffer with the specified tag
    bool ContainsBuffer(Buffer::Tag tag) const;

    /// Returns a vector of recently released buffers specified by tag
    std::vector<Buffer::Tag> GetTagsAndReleaseBuffers(size_t max_count);

    /// Returns true if the stream is currently playing
    bool IsPlaying() const {
        return state == State::Playing;
    }

    /// Returns the number of queued buffers
    size_t GetQueueSize() const {
        return queued_buffers.size();
    }

    /// Gets the sample rate
    u32 GetSampleRate() const {
        return sample_rate;
    }

    /// Gets the number of channels
    u32 GetNumChannels() const;

private:
    /// Current state of the stream
    enum class State {
        Stopped,
        Playing,
    };

    /// Plays the next queued buffer in the audio stream, starting playback if necessary
    void PlayNextBuffer();

    /// Releases the actively playing buffer, signalling that it has been completed
    void ReleaseActiveBuffer();

    /// Gets the number of core cycles when the specified buffer will be released
    s64 GetBufferReleaseCycles(const Buffer& buffer) const;

    u32 sample_rate;                        ///< Sample rate of the stream
    Format format;                          ///< Format of the stream
    ReleaseCallback release_callback;       ///< Buffer release callback for the stream
    State state{State::Stopped};            ///< Playback state of the stream
    CoreTiming::EventType* release_event{}; ///< Core timing release event for the stream
    BufferPtr active_buffer;                ///< Actively playing buffer in the stream
    std::queue<BufferPtr> queued_buffers;   ///< Buffers queued to be played in the stream
    std::queue<BufferPtr> released_buffers; ///< Buffers recently released from the stream
    SinkStream& sink_stream;                ///< Output sink for the stream
    std::string name;                       ///< Name of the stream, must be unique
};

using StreamPtr = std::shared_ptr<Stream>;

} // namespace AudioCore
