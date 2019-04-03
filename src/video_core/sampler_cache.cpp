// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/cityhash.h"
#include "common/common_types.h"
#include "video_core/sampler_cache.h"

namespace VideoCommon {

std::size_t SamplerCacheKey::Hash() const {
    static_assert(sizeof(raw) % sizeof(u64) == 0);
    return static_cast<std::size_t>(
        Common::CityHash64(reinterpret_cast<const char*>(raw.data()), sizeof(raw) / sizeof(u64)));
}

bool SamplerCacheKey::operator==(const SamplerCacheKey& rhs) const {
    return raw == rhs.raw;
}

} // namespace VideoCommon
