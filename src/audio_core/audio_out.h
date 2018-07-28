// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <vector>

#include "audio_core/buffer.h"
#include "audio_core/stream.h"
#include "common/common_types.h"

namespace AudioCore {

using StreamPtr = std::shared_ptr<Stream>;

/**
 * Represents an audio playback interface, used to open and play audio streams
 */
class AudioOut {
public:
    /// Opens a new audio stream
    StreamPtr OpenStream(int sample_rate, int num_channels,
                         Stream::ReleaseCallback&& release_callback);

    /// Returns a vector of recently released buffers specified by tag for the specified stream
    std::vector<u64> GetReleasedBuffers(StreamPtr stream, size_t max_count);

    /// Starts an audio stream for playback
    void StartStream(StreamPtr stream);

    /// Stops an audio stream that is currently playing
    void StopStream(StreamPtr stream);

    /// Queues a buffer into the specified audio stream, returns true on success
    bool QueueBuffer(StreamPtr stream, Buffer::Tag tag, std::vector<u8>&& data);

private:
    /// Active audio streams on the interface
    std::vector<StreamPtr> streams;
};

} // namespace AudioCore
