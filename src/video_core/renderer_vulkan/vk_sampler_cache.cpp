// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include <optional>
#include <unordered_map>

#include "common/assert.h"
#include "video_core/renderer_vulkan/declarations.h"
#include "video_core/renderer_vulkan/maxwell_to_vk.h"
#include "video_core/renderer_vulkan/vk_sampler_cache.h"
#include "video_core/textures/texture.h"

namespace Vulkan {

static std::optional<vk::BorderColor> TryConvertBorderColor(std::array<float, 4> color) {
    // TODO(Rodrigo): Manage integer border colors
    if (color == std::array<float, 4>{0, 0, 0, 0}) {
        return vk::BorderColor::eFloatTransparentBlack;
    } else if (color == std::array<float, 4>{0, 0, 0, 1}) {
        return vk::BorderColor::eFloatOpaqueBlack;
    } else if (color == std::array<float, 4>{1, 1, 1, 1}) {
        return vk::BorderColor::eFloatOpaqueWhite;
    } else {
        return {};
    }
}

VKSamplerCache::VKSamplerCache(const VKDevice& device) : device{device} {}

VKSamplerCache::~VKSamplerCache() = default;

UniqueSampler VKSamplerCache::CreateSampler(const Tegra::Texture::TSCEntry& tsc) const {
    const float max_anisotropy{tsc.GetMaxAnisotropy()};
    const bool has_anisotropy{max_anisotropy > 1.0f};

    const auto border_color{tsc.GetBorderColor()};
    const auto vk_border_color{TryConvertBorderColor(border_color)};
    UNIMPLEMENTED_IF_MSG(!vk_border_color, "Unimplemented border color {} {} {} {}",
                         border_color[0], border_color[1], border_color[2], border_color[3]);

    constexpr bool unnormalized_coords{false};

    const vk::SamplerCreateInfo sampler_ci(
        {}, MaxwellToVK::Sampler::Filter(tsc.mag_filter),
        MaxwellToVK::Sampler::Filter(tsc.min_filter),
        MaxwellToVK::Sampler::MipmapMode(tsc.mipmap_filter),
        MaxwellToVK::Sampler::WrapMode(tsc.wrap_u), MaxwellToVK::Sampler::WrapMode(tsc.wrap_v),
        MaxwellToVK::Sampler::WrapMode(tsc.wrap_p), tsc.GetLodBias(), has_anisotropy,
        max_anisotropy, tsc.depth_compare_enabled,
        MaxwellToVK::Sampler::DepthCompareFunction(tsc.depth_compare_func), tsc.GetMinLod(),
        tsc.GetMaxLod(), vk_border_color.value_or(vk::BorderColor::eFloatTransparentBlack),
        unnormalized_coords);

    const auto& dld{device.GetDispatchLoader()};
    const auto dev{device.GetLogical()};
    return dev.createSamplerUnique(sampler_ci, nullptr, dld);
}

vk::Sampler VKSamplerCache::ToSamplerType(const UniqueSampler& sampler) const {
    return *sampler;
}

} // namespace Vulkan
