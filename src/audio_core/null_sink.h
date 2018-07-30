// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "audio_core/sink.h"

namespace AudioCore {

class NullSink final : public Sink {
public:
    NullSink(std::string) {}
    ~NullSink() override = default;

    SinkStream& AcquireSinkStream(u32 /*sample_rate*/, u32 /*num_channels*/) override {
        return null_sink_stream;
    }

private:
    struct NullSinkStreamImpl final : SinkStream {
        void EnqueueSamples(u32 /*num_channels*/, const s16* /*samples*/,
                            size_t /*sample_count*/) override {}
    } null_sink_stream;
};

} // namespace AudioCore
