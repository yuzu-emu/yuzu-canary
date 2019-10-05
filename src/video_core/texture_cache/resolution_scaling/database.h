// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <unordered_set>
#include "video_core/surface.h"

namespace Core {
class System;
}

namespace VideoCommon::Resolution {

using VideoCore::Surface::PixelFormat;

struct ResolutionKey {
    PixelFormat format;
    u32 width;
    u32 height;
    std::size_t Hash() const {
        const std::size_t comp1 = static_cast<std::size_t>(format) << 44;
        const std::size_t comp2 = static_cast<std::size_t>(height) << 24;
        const std::size_t comp3 = static_cast<std::size_t>(width);
        return comp1 | comp2 | comp3;
    }

    bool operator==(const ResolutionKey& ks) const {
        return std::tie(format, width, height) == std::tie(ks.format, ks.width, ks.height);
    }

    bool operator!=(const ResolutionKey& ks) const {
        return !(*this == ks);
    }
};

} // namespace VideoCommon::Resolution

namespace std {

template <>
struct hash<VideoCommon::Resolution::ResolutionKey> {
    std::size_t operator()(const VideoCommon::Resolution::ResolutionKey& k) const {
        return k.Hash();
    }
};

} // namespace std

namespace VideoCommon::Resolution {

class ScalingDatabase {
public:
    explicit ScalingDatabase(Core::System& system);
    ~ScalingDatabase();

    void SaveDatabase();
    void LoadDatabase();
    void Init();

    bool IsInDatabase(const PixelFormat format, const u32 width, const u32 height) const {
        const ResolutionKey key{format, width, height};
        return database.count(key) > 0;
    }

    bool IsBlacklisted(const PixelFormat format, const u32 width, const u32 height) const {
        const ResolutionKey key{format, width, height};
        return blacklist.count(key) > 0;
    }

    void Register(const PixelFormat format, const u32 width, const u32 height);
    void Unregister(const PixelFormat format, const u32 width, const u32 height);

    std::string GetTitleID() const;
    std::string GetProfilePath() const;

private:
    std::unordered_set<ResolutionKey> database{};
    std::unordered_set<ResolutionKey> blacklist{};
    bool initialized{};
    u64 title_id{};
    Core::System& system;

    static constexpr u32 DBVersion = 1;
};

} // namespace VideoCommon::Resolution
