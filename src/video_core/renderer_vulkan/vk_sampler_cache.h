// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <unordered_map>

#include "common/common_types.h"
#include "video_core/renderer_vulkan/declarations.h"
#include "video_core/sampler_cache.h"
#include "video_core/textures/texture.h"

namespace Vulkan {

class VKDevice;

class VKSamplerCache final : public VideoCommon::SamplerCache<vk::Sampler, UniqueSampler> {
public:
    explicit VKSamplerCache(const VKDevice& device);
    ~VKSamplerCache();

protected:
    UniqueSampler CreateSampler(const Tegra::Texture::TSCEntry& tsc) const;

    vk::Sampler ToSamplerType(const UniqueSampler& sampler) const;

private:
    const VKDevice& device;
};

} // namespace Vulkan
