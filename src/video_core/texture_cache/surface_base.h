// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "common/assert.h"
#include "common/binary_find.h"
#include "common/common_types.h"
#include "video_core/gpu.h"
#include "video_core/morton.h"
#include "video_core/texture_cache/copy_params.h"
#include "video_core/texture_cache/surface_params.h"
#include "video_core/texture_cache/surface_view.h"

namespace Tegra {
class MemoryManager;
}

namespace VideoCommon {

using VideoCore::MortonSwizzleMode;
using VideoCore::Surface::SurfaceTarget;

enum class MatchStructureResult : u32 {
    FullMatch = 0,
    SemiMatch = 1,
    None = 2,
};

enum class MatchTopologyResult : u32 {
    FullMatch = 0,
    CompressUnmatch = 1,
    None = 2,
};

class StagingCache {
public:
    explicit StagingCache();
    ~StagingCache();

    std::vector<u8>& GetBuffer(std::size_t index) {
        return staging_buffer[index];
    }

    const std::vector<u8>& GetBuffer(std::size_t index) const {
        return staging_buffer[index];
    }

    void SetSize(std::size_t size) {
        staging_buffer.resize(size);
    }

private:
    std::vector<std::vector<u8>> staging_buffer;
};

class SurfaceBaseImpl {
public:
    void LoadBuffer(Tegra::MemoryManager& memory_manager, StagingCache& staging_cache);

    void FlushBuffer(Tegra::MemoryManager& memory_manager, StagingCache& staging_cache);

    GPUVAddr GetGpuAddr() const {
        return gpu_addr;
    }

    bool Overlaps(const CacheAddr start, const CacheAddr end) const {
        return (cache_addr < end) && (cache_addr_end > start);
    }

    bool IsInside(const GPUVAddr other_start, const GPUVAddr other_end) {
        const GPUVAddr gpu_addr_end = gpu_addr + guest_memory_size;
        return (gpu_addr <= other_start && other_end <= gpu_addr_end);
    }

    // Use only when recycling a surface
    void SetGpuAddr(const GPUVAddr new_addr) {
        gpu_addr = new_addr;
    }

    VAddr GetCpuAddr() const {
        return cpu_addr;
    }

    void SetCpuAddr(const VAddr new_addr) {
        cpu_addr = new_addr;
    }

    CacheAddr GetCacheAddr() const {
        return cache_addr;
    }

    CacheAddr GetCacheAddrEnd() const {
        return cache_addr_end;
    }

    void SetCacheAddr(const CacheAddr new_addr) {
        cache_addr = new_addr;
        cache_addr_end = new_addr + guest_memory_size;
    }

    const SurfaceParams& GetSurfaceParams() const {
        return params;
    }

    std::size_t GetSizeInBytes() const {
        return guest_memory_size;
    }

    std::size_t GetHostSizeInBytes() const {
        return host_memory_size;
    }

    std::size_t GetMipmapSize(const u32 level) const {
        return mipmap_sizes[level];
    }

    void MarkAsContinuous(const bool is_continuous) {
        this->is_continuous = is_continuous;
    }

    bool IsContinuous() const {
        return is_continuous;
    }

    bool IsLinear() const {
        return !params.is_tiled;
    }

    bool MatchFormat(VideoCore::Surface::PixelFormat pixel_format) const {
        return params.pixel_format == pixel_format;
    }

    VideoCore::Surface::PixelFormat GetFormat() const {
        return params.pixel_format;
    }

    bool MatchTarget(VideoCore::Surface::SurfaceTarget target) const {
        return params.target == target;
    }

    MatchTopologyResult MatchesTopology(const SurfaceParams& rhs) const;

    MatchStructureResult MatchesStructure(const SurfaceParams& rhs) const;

    bool MatchesSubTexture(const SurfaceParams& rhs, const GPUVAddr other_gpu_addr) const {
        return std::tie(gpu_addr, params.target, params.num_levels) ==
                   std::tie(other_gpu_addr, rhs.target, rhs.num_levels) &&
               params.target == SurfaceTarget::Texture2D && params.num_levels == 1;
    }

    std::optional<std::pair<u32, u32>> GetLayerMipmap(const GPUVAddr candidate_gpu_addr) const;

    std::vector<CopyParams> BreakDown(const SurfaceParams& in_params) const {
        return params.is_layered ? BreakDownLayered(in_params) : BreakDownNonLayered(in_params);
    }

protected:
    explicit SurfaceBaseImpl(GPUVAddr gpu_addr, const SurfaceParams& params);
    ~SurfaceBaseImpl() = default;

    virtual void DecorateSurfaceName() = 0;

    const SurfaceParams params;
    std::size_t layer_size;
    std::size_t guest_memory_size;
    const std::size_t host_memory_size;
    GPUVAddr gpu_addr{};
    CacheAddr cache_addr{};
    CacheAddr cache_addr_end{};
    VAddr cpu_addr{};
    bool is_continuous{};

    std::vector<std::size_t> mipmap_sizes;
    std::vector<std::size_t> mipmap_offsets;

private:
    void SwizzleFunc(MortonSwizzleMode mode, u8* memory, const SurfaceParams& params, u8* buffer,
                     u32 level);

    std::vector<CopyParams> BreakDownLayered(const SurfaceParams& in_params) const;

    std::vector<CopyParams> BreakDownNonLayered(const SurfaceParams& in_params) const;
};

template <typename TView>
class SurfaceBase : public SurfaceBaseImpl {
public:
    virtual void UploadTexture(const std::vector<u8>& staging_buffer) = 0;

    virtual void DownloadTexture(std::vector<u8>& staging_buffer) = 0;

    void MarkAsModified(bool is_modified_, u64 tick) {
        is_modified = is_modified_ || is_target;
        modification_tick = tick;
    }

    void MarkAsRenderTarget(bool is_target_, u32 index_) {
        is_target = is_target_;
        index = index_;
    }

    void MarkAsRescaled(const bool is_rescaled) {
        this->is_rescaled = is_rescaled;
    }

    void MarkAsPicked(bool is_picked_) {
        is_picked = is_picked_;
    }

    bool IsModified() const {
        return is_modified;
    }

    bool IsProtected() const {
        // Only 3D Slices are to be protected
        return is_target && params.block_depth > 0;
    }

    bool IsRenderTarget() const {
        return is_target;
    }

    u32 GetRenderTarget() const {
        return index;
    }

    bool IsRescaled() const {
        return is_rescaled;
    }

    bool IsRegistered() const {
        return is_registered;
    }

    bool IsPicked() const {
        return is_picked;
    }

    void MarkAsRegistered(bool is_reg) {
        is_registered = is_reg;
    }

    u64 GetModificationTick() const {
        return modification_tick;
    }

    TView EmplaceOverview(const SurfaceParams& overview_params) {
        const u32 num_layers{(params.is_layered && !overview_params.is_layered) ? 1 : params.depth};
        return GetView(ViewParams(overview_params.target, 0, num_layers, 0, params.num_levels));
    }

    std::optional<TView> EmplaceIrregularView(const SurfaceParams& view_params,
                                              const GPUVAddr view_addr,
                                              const std::size_t candidate_size, const u32 mipmap,
                                              const u32 layer) {
        const auto layer_mipmap{GetLayerMipmap(view_addr + candidate_size)};
        if (!layer_mipmap) {
            return {};
        }
        const u32 end_layer{layer_mipmap->first};
        const u32 end_mipmap{layer_mipmap->second};
        if (layer != end_layer) {
            if (mipmap == 0 && end_mipmap == 0) {
                return GetView(ViewParams(view_params.target, layer, end_layer - layer + 1, 0, 1));
            }
            return {};
        } else {
            return GetView(
                ViewParams(view_params.target, layer, 1, mipmap, end_mipmap - mipmap + 1));
        }
    }

    std::optional<TView> EmplaceView(const SurfaceParams& view_params, const GPUVAddr view_addr,
                                     const std::size_t candidate_size) {
        if (params.target == SurfaceTarget::Texture3D ||
            (params.num_levels == 1 && !params.is_layered) ||
            view_params.target == SurfaceTarget::Texture3D) {
            return {};
        }
        const auto layer_mipmap{GetLayerMipmap(view_addr)};
        if (!layer_mipmap) {
            return {};
        }
        const u32 layer{layer_mipmap->first};
        const u32 mipmap{layer_mipmap->second};
        if (GetMipmapSize(mipmap) != candidate_size) {
            return EmplaceIrregularView(view_params, view_addr, candidate_size, mipmap, layer);
        }
        return GetView(ViewParams(view_params.target, layer, 1, mipmap, 1));
    }

    TView GetMainView() const {
        return main_view;
    }

protected:
    explicit SurfaceBase(const GPUVAddr gpu_addr, const SurfaceParams& params)
        : SurfaceBaseImpl(gpu_addr, params) {}

    ~SurfaceBase() = default;

    virtual TView CreateView(const ViewParams& view_key) = 0;

    TView main_view;
    std::unordered_map<ViewParams, TView> views;

private:
    TView GetView(const ViewParams& key) {
        const auto [entry, is_cache_miss] = views.try_emplace(key);
        auto& view{entry->second};
        if (is_cache_miss) {
            view = CreateView(key);
        }
        return view;
    }

    static constexpr u32 NO_RT = 0xFFFFFFFF;

    bool is_modified{};
    bool is_target{};
    bool is_registered{};
    bool is_picked{};
    bool is_rescaled{};
    u32 index{NO_RT};
    u64 modification_tick{};
};

} // namespace VideoCommon
