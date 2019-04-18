// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <unordered_map>

#include "video_core/textures/texture.h"

namespace VideoCommon {

struct SamplerCacheKey final : public Tegra::Texture::TSCEntry {
    std::size_t Hash() const;

    bool operator==(const SamplerCacheKey& rhs) const;

    bool operator!=(const SamplerCacheKey& rhs) const {
        return !operator==(rhs);
    }
};

} // namespace VideoCommon

namespace std {

template <>
struct hash<VideoCommon::SamplerCacheKey> {
    std::size_t operator()(const VideoCommon::SamplerCacheKey& k) const noexcept {
        return k.Hash();
    }
};

} // namespace std

namespace VideoCommon {

template <typename SamplerType, typename SamplerStorageType>
class SamplerCache {
public:
    SamplerType GetSampler(const Tegra::Texture::TSCEntry& tsc) {
        const auto [entry, is_cache_miss] = cache.try_emplace(SamplerCacheKey{tsc});
        auto& sampler = entry->second;
        if (is_cache_miss) {
            sampler = CreateSampler(tsc);
        }
        return ToSamplerType(sampler);
    }

protected:
    virtual SamplerStorageType CreateSampler(const Tegra::Texture::TSCEntry& tsc) const = 0;

    virtual SamplerType ToSamplerType(const SamplerStorageType& sampler) const = 0;

private:
    std::unordered_map<SamplerCacheKey, SamplerStorageType> cache;
};

} // namespace VideoCommon