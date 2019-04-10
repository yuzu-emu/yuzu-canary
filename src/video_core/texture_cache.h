// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <list>
#include <memory>
#include <set>
#include <tuple>
#include <type_traits>
#include <unordered_map>

#include <boost/icl/interval_map.hpp>
#include <boost/range/iterator_range.hpp>

#include "common/assert.h"
#include "common/common_types.h"
#include "core/memory.h"
#include "video_core/engines/fermi_2d.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/gpu.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/surface.h"

namespace Core {
class System;
}

namespace Tegra::Texture {
struct FullTextureInfo;
}

namespace VideoCore {
class RasterizerInterface;
}

namespace VideoCommon {

class HasheableSurfaceParams {
public:
    std::size_t Hash() const;

    bool operator==(const HasheableSurfaceParams& rhs) const;

protected:
    // Avoid creation outside of a managed environment.
    HasheableSurfaceParams() = default;

    bool is_tiled;
    u32 block_width;
    u32 block_height;
    u32 block_depth;
    u32 tile_width_spacing;
    u32 width;
    u32 height;
    u32 depth;
    u32 pitch;
    u32 unaligned_height;
    u32 num_levels;
    VideoCore::Surface::PixelFormat pixel_format;
    VideoCore::Surface::ComponentType component_type;
    VideoCore::Surface::SurfaceType type;
    VideoCore::Surface::SurfaceTarget target;
};

class SurfaceParams final : public HasheableSurfaceParams {
public:
    /// Creates SurfaceCachedParams from a texture configuration.
    static SurfaceParams CreateForTexture(Core::System& system,
                                          const Tegra::Texture::FullTextureInfo& config);

    /// Creates SurfaceCachedParams for a depth buffer configuration.
    static SurfaceParams CreateForDepthBuffer(
        Core::System& system, u32 zeta_width, u32 zeta_height, Tegra::DepthFormat format,
        u32 block_width, u32 block_height, u32 block_depth,
        Tegra::Engines::Maxwell3D::Regs::InvMemoryLayout type);

    /// Creates SurfaceCachedParams from a framebuffer configuration.
    static SurfaceParams CreateForFramebuffer(Core::System& system, std::size_t index);

    /// Creates SurfaceCachedParams from a Fermi2D surface configuration.
    static SurfaceParams CreateForFermiCopySurface(
        const Tegra::Engines::Fermi2D::Regs::Surface& config);

    bool IsTiled() const {
        return is_tiled;
    }

    u32 GetBlockWidth() const {
        return block_width;
    }

    u32 GetTileWidthSpacing() const {
        return tile_width_spacing;
    }

    u32 GetWidth() const {
        return width;
    }

    u32 GetHeight() const {
        return height;
    }

    u32 GetDepth() const {
        return depth;
    }

    u32 GetPitch() const {
        return pitch;
    }

    u32 GetNumLevels() const {
        return num_levels;
    }

    VideoCore::Surface::PixelFormat GetPixelFormat() const {
        return pixel_format;
    }

    VideoCore::Surface::ComponentType GetComponentType() const {
        return component_type;
    }

    VideoCore::Surface::SurfaceTarget GetTarget() const {
        return target;
    }

    VideoCore::Surface::SurfaceType GetType() const {
        return type;
    }

    std::size_t GetGuestSizeInBytes() const {
        return guest_size_in_bytes;
    }

    std::size_t GetHostSizeInBytes() const {
        return host_size_in_bytes;
    }

    u32 GetNumLayers() const {
        return num_layers;
    }

    /// Returns the width of a given mipmap level.
    u32 GetMipWidth(u32 level) const;

    /// Returns the height of a given mipmap level.
    u32 GetMipHeight(u32 level) const;

    /// Returns the depth of a given mipmap level.
    u32 GetMipDepth(u32 level) const;

    /// Returns true if these parameters are from a layered surface.
    bool IsLayered() const;

    /// Returns the block height of a given mipmap level.
    u32 GetMipBlockHeight(u32 level) const;

    /// Returns the block depth of a given mipmap level.
    u32 GetMipBlockDepth(u32 level) const;

    /// Returns the offset in bytes in guest memory of a given mipmap level.
    std::size_t GetGuestMipmapLevelOffset(u32 level) const;

    /// Returns the offset in bytes in host memory (linear) of a given mipmap level.
    std::size_t GetHostMipmapLevelOffset(u32 level) const;

    /// Returns the size of a layer in bytes in guest memory.
    std::size_t GetGuestLayerSize() const;

    /// Returns the size of a layer in bytes in host memory for a given mipmap level.
    std::size_t GetHostLayerSize(u32 level) const;

    /// Returns true if another surface can be familiar with this. This is a loosely defined term
    /// that reflects the possibility of these two surface parameters potentially being part of a
    /// bigger superset.
    bool IsFamiliar(const SurfaceParams& view_params) const;

    /// Returns true if the pixel format is a depth and/or stencil format.
    bool IsPixelFormatZeta() const;

    /// Creates a map that redirects an address difference to a layer and mipmap level.
    std::map<u64, std::pair<u32, u32>> CreateViewOffsetMap() const;

    /// Returns true if the passed surface view parameters is equal or a valid subset of this.
    bool IsViewValid(const SurfaceParams& view_params, u32 layer, u32 level) const;

private:
    /// Calculates values that can be deduced from HasheableSurfaceParams.
    void CalculateCachedValues();

    /// Returns the size of a given mipmap level.
    std::size_t GetInnerMipmapMemorySize(u32 level, bool as_host_size, bool layer_only,
                                         bool uncompressed) const;

    /// Returns the size of all mipmap levels and aligns as needed.
    std::size_t GetInnerMemorySize(bool as_host_size, bool layer_only, bool uncompressed) const;

    /// Returns true if the passed view width and height match the size of this params in a given
    /// mipmap level.
    bool IsDimensionValid(const SurfaceParams& view_params, u32 level) const;

    /// Returns true if the passed view depth match the size of this params in a given mipmap level.
    bool IsDepthValid(const SurfaceParams& view_params, u32 level) const;

    /// Returns true if the passed view layers and mipmap levels are in bounds.
    bool IsInBounds(const SurfaceParams& view_params, u32 layer, u32 level) const;

    std::size_t guest_size_in_bytes;
    std::size_t host_size_in_bytes;
    u32 num_layers;
};

struct ViewKey {
    std::size_t Hash() const;

    bool operator==(const ViewKey& rhs) const;

    u32 base_layer{};
    u32 num_layers{};
    u32 base_level{};
    u32 num_levels{};
};

} // namespace VideoCommon

namespace std {

template <>
struct hash<VideoCommon::SurfaceParams> {
    std::size_t operator()(const VideoCommon::SurfaceParams& k) const noexcept {
        return k.Hash();
    }
};

template <>
struct hash<VideoCommon::ViewKey> {
    std::size_t operator()(const VideoCommon::ViewKey& k) const noexcept {
        return k.Hash();
    }
};

} // namespace std

namespace VideoCommon {

template <typename TView, typename TExecutionContext>
class SurfaceBase {
    static_assert(std::is_trivially_copyable_v<TExecutionContext>);

public:
    virtual void LoadBuffer() = 0;

    virtual TExecutionContext FlushBuffer(TExecutionContext exctx) = 0;

    virtual TExecutionContext UploadTexture(TExecutionContext exctx) = 0;

    TView* TryGetView(VAddr view_addr, const SurfaceParams& view_params) {
        if (view_addr < cpu_addr || !params.IsFamiliar(view_params)) {
            // It can't be a view if it's in a prior address.
            return {};
        }

        const auto relative_offset{static_cast<u64>(view_addr - cpu_addr)};
        const auto it{view_offset_map.find(relative_offset)};
        if (it == view_offset_map.end()) {
            // Couldn't find an aligned view.
            return {};
        }
        const auto [layer, level] = it->second;

        if (!params.IsViewValid(view_params, layer, level)) {
            return {};
        }

        return GetView(layer, view_params.GetNumLayers(), level, view_params.GetNumLevels());
    }

    VAddr GetCpuAddr() const {
        ASSERT(is_registered);
        return cpu_addr;
    }

    u8* GetHostPtr() const {
        ASSERT(is_registered);
        return host_ptr;
    }

    CacheAddr GetCacheAddr() const {
        ASSERT(is_registered);
        return cache_addr;
    }

    std::size_t GetSizeInBytes() const {
        return params.GetGuestSizeInBytes();
    }

    void MarkAsModified(bool is_modified_) {
        is_modified = is_modified_;
    }

    const SurfaceParams& GetSurfaceParams() const {
        return params;
    }

    TView* GetView(VAddr view_addr, const SurfaceParams& view_params) {
        TView* view{TryGetView(view_addr, view_params)};
        ASSERT(view != nullptr);
        return view;
    }

    void Register(VAddr cpu_addr_, u8* host_ptr_) {
        ASSERT(!is_registered);
        is_registered = true;
        cpu_addr = cpu_addr_;
        host_ptr = host_ptr_;
        cache_addr = ToCacheAddr(host_ptr_);
    }

    void Register(VAddr cpu_addr_) {
        Register(cpu_addr_, Memory::GetPointer(cpu_addr_));
    }

    void Unregister() {
        ASSERT(is_registered);
        is_registered = false;
    }

    bool IsRegistered() const {
        return is_registered;
    }

protected:
    explicit SurfaceBase(const SurfaceParams& params)
        : params{params}, view_offset_map{params.CreateViewOffsetMap()} {}

    ~SurfaceBase() = default;

    virtual std::unique_ptr<TView> CreateView(const ViewKey& view_key) = 0;

    bool IsModified() const {
        return is_modified;
    }

    const SurfaceParams params;

private:
    TView* GetView(u32 base_layer, u32 num_layers, u32 base_level, u32 num_levels) {
        const ViewKey key{base_layer, num_layers, base_level, num_levels};
        const auto [entry, is_cache_miss] = views.try_emplace(key);
        auto& view{entry->second};
        if (is_cache_miss) {
            view = CreateView(key);
        }
        return view.get();
    }

    const std::map<u64, std::pair<u32, u32>> view_offset_map;

    VAddr cpu_addr{};
    u8* host_ptr{};
    CacheAddr cache_addr{};
    bool is_modified{};
    bool is_registered{};
    std::unordered_map<ViewKey, std::unique_ptr<TView>> views;
};

template <typename TSurface, typename TView, typename TExecutionContext>
class TextureCache {
    static_assert(std::is_trivially_copyable_v<TExecutionContext>);
    using ResultType = std::tuple<TView*, TExecutionContext>;
    using IntervalMap = boost::icl::interval_map<CacheAddr, std::set<TSurface*>>;
    using IntervalType = typename IntervalMap::interval_type;

public:
    void InvalidateRegion(CacheAddr addr, std::size_t size) {
        for (TSurface* surface : GetSurfacesInRegion(addr, size)) {
            if (!surface->IsRegistered()) {
                // Skip duplicates
                continue;
            }
            Unregister(surface);
        }
    }

    ResultType GetTextureSurface(TExecutionContext exctx,
                                 const Tegra::Texture::FullTextureInfo& config) {
        auto& memory_manager{system.GPU().MemoryManager()};
        const auto cpu_addr{memory_manager.GpuToCpuAddress(config.tic.Address())};
        if (!cpu_addr) {
            return {{}, exctx};
        }
        const auto params{SurfaceParams::CreateForTexture(system, config)};
        return GetSurfaceView(exctx, *cpu_addr, params, true);
    }

    ResultType GetDepthBufferSurface(TExecutionContext exctx, bool preserve_contents) {
        const auto& regs{system.GPU().Maxwell3D().regs};
        if (!regs.zeta.Address() || !regs.zeta_enable) {
            return {{}, exctx};
        }

        auto& memory_manager{system.GPU().MemoryManager()};
        const auto cpu_addr{memory_manager.GpuToCpuAddress(regs.zeta.Address())};
        if (!cpu_addr) {
            return {{}, exctx};
        }

        const auto depth_params{SurfaceParams::CreateForDepthBuffer(
            system, regs.zeta_width, regs.zeta_height, regs.zeta.format,
            regs.zeta.memory_layout.block_width, regs.zeta.memory_layout.block_height,
            regs.zeta.memory_layout.block_depth, regs.zeta.memory_layout.type)};
        return GetSurfaceView(exctx, *cpu_addr, depth_params, preserve_contents);
    }

    ResultType GetColorBufferSurface(TExecutionContext exctx, std::size_t index,
                                     bool preserve_contents) {
        ASSERT(index < Tegra::Engines::Maxwell3D::Regs::NumRenderTargets);

        const auto& regs{system.GPU().Maxwell3D().regs};
        if (index >= regs.rt_control.count || regs.rt[index].Address() == 0 ||
            regs.rt[index].format == Tegra::RenderTargetFormat::NONE) {
            return {{}, exctx};
        }

        auto& memory_manager{system.GPU().MemoryManager()};
        const auto& config{system.GPU().Maxwell3D().regs.rt[index]};
        const auto cpu_addr{memory_manager.GpuToCpuAddress(
            config.Address() + config.base_layer * config.layer_stride * sizeof(u32))};
        if (!cpu_addr) {
            return {{}, exctx};
        }

        return GetSurfaceView(exctx, *cpu_addr, SurfaceParams::CreateForFramebuffer(system, index),
                              preserve_contents);
    }

    ResultType GetFermiSurface(TExecutionContext exctx,
                               const Tegra::Engines::Fermi2D::Regs::Surface& config) {
        const auto cpu_addr{system.GPU().MemoryManager().GpuToCpuAddress(config.Address())};
        ASSERT(cpu_addr);
        return GetSurfaceView(exctx, *cpu_addr, SurfaceParams::CreateForFermiCopySurface(config),
                              true);
    }

    TSurface* TryFindFramebufferSurface(const u8* host_ptr) const {
        const auto it{registered_surfaces.find(ToCacheAddr(host_ptr))};
        return it != registered_surfaces.end() ? *it->second.begin() : nullptr;
    }

protected:
    TextureCache(Core::System& system, VideoCore::RasterizerInterface& rasterizer)
        : system{system}, rasterizer{rasterizer} {}

    ~TextureCache() = default;

    virtual ResultType TryFastGetSurfaceView(TExecutionContext exctx, VAddr cpu_addr, u8* host_ptr,
                                             const SurfaceParams& params, bool preserve_contents,
                                             const std::vector<TSurface*>& overlaps) = 0;

    virtual std::unique_ptr<TSurface> CreateSurface(const SurfaceParams& params) = 0;

    void Register(TSurface* surface, VAddr cpu_addr, u8* host_ptr) {
        surface->Register(cpu_addr, host_ptr);
        registered_surfaces.add({GetSurfaceInterval(surface), {surface}});
        rasterizer.UpdatePagesCachedCount(surface->GetCpuAddr(), surface->GetSizeInBytes(), 1);
    }

    void Unregister(TSurface* surface) {
        registered_surfaces.subtract({GetSurfaceInterval(surface), {surface}});
        rasterizer.UpdatePagesCachedCount(surface->GetCpuAddr(), surface->GetSizeInBytes(), -1);
        surface->Unregister();
    }

    TSurface* GetUncachedSurface(const SurfaceParams& params) {
        if (TSurface* surface = TryGetReservedSurface(params); surface)
            return surface;
        // No reserved surface available, create a new one and reserve it
        auto new_surface{CreateSurface(params)};
        TSurface* surface{new_surface.get()};
        ReserveSurface(params, std::move(new_surface));
        return surface;
    }

    Core::System& system;

private:
    ResultType GetSurfaceView(TExecutionContext exctx, VAddr cpu_addr, const SurfaceParams& params,
                              bool preserve_contents) {
        const auto host_ptr{Memory::GetPointer(cpu_addr)};
        const auto cache_addr{ToCacheAddr(host_ptr)};
        const auto overlaps{GetSurfacesInRegion(cache_addr, params.GetGuestSizeInBytes())};
        if (overlaps.empty()) {
            return LoadSurfaceView(exctx, cpu_addr, host_ptr, params, preserve_contents);
        }

        if (overlaps.size() == 1) {
            if (TView* view = overlaps[0]->TryGetView(cpu_addr, params); view)
                return {view, exctx};
        }

        TView* fast_view;
        std::tie(fast_view, exctx) =
            TryFastGetSurfaceView(exctx, cpu_addr, host_ptr, params, preserve_contents, overlaps);

        for (TSurface* surface : overlaps) {
            if (!fast_view) {
                // Flush even when we don't care about the contents, to preserve memory not written
                // by the new surface.
                exctx = surface->FlushBuffer(exctx);
            }
            Unregister(surface);
        }

        if (fast_view) {
            return {fast_view, exctx};
        }

        return LoadSurfaceView(exctx, cpu_addr, host_ptr, params, preserve_contents);
    }

    ResultType LoadSurfaceView(TExecutionContext exctx, VAddr cpu_addr, u8* host_ptr,
                               const SurfaceParams& params, bool preserve_contents) {
        TSurface* new_surface{GetUncachedSurface(params)};
        Register(new_surface, cpu_addr, host_ptr);
        if (preserve_contents) {
            exctx = LoadSurface(exctx, new_surface);
        }
        return {new_surface->GetView(cpu_addr, params), exctx};
    }

    TExecutionContext LoadSurface(TExecutionContext exctx, TSurface* surface) {
        surface->LoadBuffer();
        exctx = surface->UploadTexture(exctx);
        surface->MarkAsModified(false);
        return exctx;
    }

    std::vector<TSurface*> GetSurfacesInRegion(CacheAddr cache_addr, std::size_t size) const {
        if (size == 0) {
            return {};
        }
        const IntervalType interval{cache_addr, cache_addr + size};

        std::vector<TSurface*> surfaces;
        for (auto& pair : boost::make_iterator_range(registered_surfaces.equal_range(interval))) {
            surfaces.push_back(*pair.second.begin());
        }
        return surfaces;
    }

    void ReserveSurface(const SurfaceParams& params, std::unique_ptr<TSurface> surface) {
        surface_reserve[params].push_back(std::move(surface));
    }

    TSurface* TryGetReservedSurface(const SurfaceParams& params) {
        auto search{surface_reserve.find(params)};
        if (search == surface_reserve.end()) {
            return {};
        }
        for (auto& surface : search->second) {
            if (!surface->IsRegistered()) {
                return surface.get();
            }
        }
        return {};
    }

    IntervalType GetSurfaceInterval(TSurface* surface) const {
        return IntervalType::right_open(surface->GetCacheAddr(),
                                        surface->GetCacheAddr() + surface->GetSizeInBytes());
    }

    VideoCore::RasterizerInterface& rasterizer;

    IntervalMap registered_surfaces;

    /// The surface reserve is a "backup" cache, this is where we put unique surfaces that have
    /// previously been used. This is to prevent surfaces from being constantly created and
    /// destroyed when used with different surface parameters.
    std::unordered_map<SurfaceParams, std::list<std::unique_ptr<TSurface>>> surface_reserve;
};

} // namespace VideoCommon
