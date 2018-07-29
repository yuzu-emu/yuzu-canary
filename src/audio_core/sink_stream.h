// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>

#include "common/common_types.h"

namespace AudioCore {

/**
 * Accepts samples in stereo signed PCM16 format to be output. Sinks *do not* handle resampling and
 * expect the correct sample rate. They are dumb outputs.
 */
class SinkStream {
public:
    virtual ~SinkStream() = default;

    /**
     * Feed stereo samples to sink.
     * @param num_channels Number of channels used.
     * @param samples Samples in interleaved stereo PCM16 format.
     * @param sample_count Number of samples.
     */
    virtual void EnqueueSamples(u32 num_channels, const s16* samples, size_t sample_count) = 0;
};

using SinkStreamPtr = std::unique_ptr<SinkStream>;

} // namespace AudioCore
