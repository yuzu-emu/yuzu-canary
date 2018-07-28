// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <vector>

#include "common/common_types.h"

namespace AudioCore {

/**
 * Represents a buffer of audio samples to be played in an audio stream
 */
class Buffer {
public:
    using Tag = u64;

    Buffer(Tag tag, std::vector<u8>&& data) : tag{tag}, data{std::move(data)} {}

    /// Returns the raw audio data for the buffer
    const std::vector<u8>& GetData() const {
        return data;
    }

    /// Returns the buffer tag, this is provided by the game to the audout service
    Tag GetTag() const {
        return tag;
    }

private:
    Tag tag;
    std::vector<u8> data;
};

} // namespace AudioCore
