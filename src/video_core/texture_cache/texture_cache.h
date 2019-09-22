// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <set>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <fmt/format.h>

#include <boost/icl/interval_map.hpp>
#include <boost/range/iterator_range.hpp>

#include "common/assert.h"
#include "common/common_types.h"
#include "common/math_util.h"
#include "core/core.h"
#include "core/memory.h"
#include "core/settings.h"
#include "video_core/engines/fermi_2d.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/gpu.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/surface.h"
#include "video_core/texture_cache/copy_params.h"
#include "video_core/texture_cache/resolution_scaling/database.h"
#include "video_core/texture_cache/surface_base.h"
#include "video_core/texture_cache/surface_params.h"
#include "video_core/texture_cache/surface_view.h"

namespace Tegra::Texture {
struct FullTextureInfo;
}

namespace VideoCore {
class RasterizerInterface;
}

namespace VideoCommon {

using VideoCore::Surface::PixelFormat;

using VideoCore::Surface::SurfaceTarget;
using RenderTargetConfig = Tegra::Engines::Maxwell3D::Regs::RenderTargetConfig;

template <typename TSurface, typename TView>
class TextureCache {
    using IntervalMap = boost::icl::interval_map<CacheAddr, std::set<TSurface>>;
    using IntervalType = typename IntervalMap::interval_type;

private:
    enum class UnregisterReason : u32 {
        Invalidated,
        Recycled,
        Rebuilt,
        Restructured,
    };

public:
    void InvalidateRegion(CacheAddr addr, std::size_t size) {
        std::lock_guard lock{mutex};

        for (const auto& surface : GetSurfacesInRegion(addr, size)) {
            Unregister(surface, UnregisterReason::Invalidated);
        }
    }

    void LoadResources() {
        scaling_database.Init();
    }

    /***
     * `Guard` guarantees that rendertargets don't unregister themselves if the
     * collide. Protection is currently only done on 3D slices.
     ***/
    void GuardRenderTargets(bool new_guard) {
        guard_render_targets = new_guard;
    }

    void GuardSamplers(bool new_guard) {
        guard_samplers = new_guard;
    }

    void FlushRegion(CacheAddr addr, std::size_t size) {
        std::lock_guard lock{mutex};

        auto surfaces = GetSurfacesInRegion(addr, size);
        if (surfaces.empty()) {
            return;
        }
        std::sort(surfaces.begin(), surfaces.end(), [](const TSurface& a, const TSurface& b) {
            return a->GetModificationTick() < b->GetModificationTick();
        });
        for (const auto& surface : surfaces) {
            FlushSurface(surface);
        }
    }

    TView GetTextureSurface(const Tegra::Texture::TICEntry& tic,
                            const VideoCommon::Shader::Sampler& entry) {
        std::lock_guard lock{mutex};
        const auto gpu_addr{tic.Address()};
        if (!gpu_addr) {
            return {};
        }
        const auto params{SurfaceParams::CreateForTexture(tic, entry)};
        const auto [surface, view] = GetSurface(gpu_addr, params, true, false);
        if (guard_samplers) {
            sampled_textures.push_back(surface);
        }
        return view;
    }

    TView GetImageSurface(const Tegra::Texture::TICEntry& tic,
                          const VideoCommon::Shader::Image& entry) {
        std::lock_guard lock{mutex};
        const auto gpu_addr{tic.Address()};
        if (!gpu_addr) {
            return {};
        }
        const auto params{SurfaceParams::CreateForImage(tic, entry)};
        const auto [surface, view] = GetSurface(gpu_addr, params, true, false);
        if (guard_samplers) {
            sampled_textures.push_back(surface);
        }
        return view;
    }

    bool TextureBarrier() {
        const bool any_rt =
            std::any_of(sampled_textures.begin(), sampled_textures.end(),
                        [](const auto& surface) { return surface->IsRenderTarget(); });
        sampled_textures.clear();
        return any_rt;
    }

    TView GetDepthBufferSurface(bool preserve_contents) {
        std::lock_guard lock{mutex};
        auto& maxwell3d = system.GPU().Maxwell3D();

        if (!maxwell3d.dirty.depth_buffer) {
            return depth_buffer.view;
        }
        maxwell3d.dirty.depth_buffer = false;

        const auto& regs{maxwell3d.regs};
        const auto gpu_addr{regs.zeta.Address()};
        if (!gpu_addr || !regs.zeta_enable) {
            SetEmptyDepthBuffer();
            return {};
        }
        const auto depth_params{SurfaceParams::CreateForDepthBuffer(
            system, regs.zeta_width, regs.zeta_height, regs.zeta.format,
            regs.zeta.memory_layout.block_width, regs.zeta.memory_layout.block_height,
            regs.zeta.memory_layout.block_depth, regs.zeta.memory_layout.type)};
        auto surface_view = GetSurface(gpu_addr, depth_params, preserve_contents, true);
        if (depth_buffer.target)
            depth_buffer.target->MarkAsRenderTarget(false, NO_RT);
        depth_buffer.target = surface_view.first;
        depth_buffer.view = surface_view.second;
        if (depth_buffer.target) {
            depth_buffer.target->MarkAsRenderTarget(true, DEPTH_RT);
            if (IsResolutionScannerEnabled()) {
                MarkScanner(depth_buffer.target);
            }
        }
        return surface_view.second;
    }

    TView GetColorBufferSurface(std::size_t index, bool preserve_contents) {
        std::lock_guard lock{mutex};
        ASSERT(index < Tegra::Engines::Maxwell3D::Regs::NumRenderTargets);
        auto& maxwell3d = system.GPU().Maxwell3D();
        if (!maxwell3d.dirty.render_target[index]) {
            return render_targets[index].view;
        }
        maxwell3d.dirty.render_target[index] = false;

        const auto& regs{maxwell3d.regs};
        if (index >= regs.rt_control.count || regs.rt[index].Address() == 0 ||
            regs.rt[index].format == Tegra::RenderTargetFormat::NONE) {
            SetEmptyColorBuffer(index);
            return {};
        }

        const auto& config{regs.rt[index]};
        const auto gpu_addr{config.Address()};
        if (!gpu_addr) {
            SetEmptyColorBuffer(index);
            return {};
        }

        auto surface_view = GetSurface(gpu_addr, SurfaceParams::CreateForFramebuffer(system, index),
                                       preserve_contents, true);
        if (render_targets[index].target)
            render_targets[index].target->MarkAsRenderTarget(false, NO_RT);
        render_targets[index].target = surface_view.first;
        render_targets[index].view = surface_view.second;
        if (render_targets[index].target) {
            render_targets[index].target->MarkAsRenderTarget(true, static_cast<u32>(index));
            if (IsResolutionScannerEnabled()) {
                MarkScanner(render_targets[index].target);
            }
        }
        return surface_view.second;
    }

    void MarkColorBufferInUse(std::size_t index) {
        if (auto& render_target = render_targets[index].target) {
            render_target->MarkAsModified(true, Tick());
        }
    }

    void MarkDepthBufferInUse() {
        if (depth_buffer.target) {
            depth_buffer.target->MarkAsModified(true, Tick());
        }
    }

    void SetEmptyDepthBuffer() {
        if (depth_buffer.target == nullptr) {
            return;
        }
        depth_buffer.target->MarkAsRenderTarget(false, NO_RT);
        depth_buffer.target = nullptr;
        depth_buffer.view = nullptr;
    }

    void SetEmptyColorBuffer(std::size_t index) {
        if (render_targets[index].target == nullptr) {
            return;
        }
        render_targets[index].target->MarkAsRenderTarget(false, NO_RT);
        render_targets[index].target = nullptr;
        render_targets[index].view = nullptr;
    }

    void DoFermiCopy(const Tegra::Engines::Fermi2D::Regs::Surface& src_config,
                     const Tegra::Engines::Fermi2D::Regs::Surface& dst_config,
                     const Tegra::Engines::Fermi2D::Config& copy_config) {
        std::lock_guard lock{mutex};
        std::pair<TSurface, TView> dst_surface = GetFermiSurface(dst_config);
        std::pair<TSurface, TView> src_surface = GetFermiSurface(src_config);
        if (IsResolutionScannerEnabled()) {
            bool is_candidate = IsInRSDatabase(src_surface.first);
            if (is_candidate) {
                MarkScanner(dst_surface.first);
            }
        }
        ImageBlit(src_surface.second, dst_surface.second, copy_config);
        dst_surface.first->MarkAsModified(true, Tick());
    }

    TSurface TryFindFramebufferSurface(const u8* host_ptr) {
        const CacheAddr cache_addr = ToCacheAddr(host_ptr);
        if (!cache_addr) {
            return nullptr;
        }
        const CacheAddr page = cache_addr >> registry_page_bits;
        std::vector<TSurface>& list = registry[page];
        for (auto& surface : list) {
            if (surface->GetCacheAddr() == cache_addr) {
                return surface;
            }
        }
        return nullptr;
    }

    u64 Tick() {
        return ++ticks;
    }

    bool IsResolutionScalingEnabled() {
        if (IsResolutionScannerEnabled()) {
            return CheckBlackListMatch();
        }
        if (!EnabledRescaling()) {
            return false;
        }
        return CheckResolutionScalingEnabled();
    }

    bool IsResolutionScalingEnabledRT(const std::size_t index) {
        if (!EnabledRescaling()) {
            return false;
        }
        if (render_targets[index].target) {
            return render_targets[index].target->IsRescaled();
        }
        return false;
    }

    bool IsResolutionScalingEnabledDB() {
        if (!EnabledRescaling()) {
            return false;
        }
        if (depth_buffer.target) {
            return depth_buffer.target->IsRescaled();
        }
        return false;
    }

protected:
    TextureCache(Core::System& system, VideoCore::RasterizerInterface& rasterizer)
        : system{system}, rasterizer{rasterizer}, scaling_database{system} {
        for (std::size_t i = 0; i < Tegra::Engines::Maxwell3D::Regs::NumRenderTargets; i++) {
            SetEmptyColorBuffer(i);
        }

        enable_resolution_scaling =
            Settings::values.resolution_factor != 1.0f && !Settings::values.use_resolution_scanner;

        SetEmptyDepthBuffer();
        staging_cache.SetSize(2);

        const auto make_siblings = [this](PixelFormat a, PixelFormat b) {
            siblings_table[static_cast<std::size_t>(a)] = b;
            siblings_table[static_cast<std::size_t>(b)] = a;
        };
        std::fill(siblings_table.begin(), siblings_table.end(), PixelFormat::Invalid);
        make_siblings(PixelFormat::Z16, PixelFormat::R16U);
        make_siblings(PixelFormat::Z32F, PixelFormat::R32F);
        make_siblings(PixelFormat::Z32FS8, PixelFormat::RG32F);

        sampled_textures.reserve(64);
    }

    ~TextureCache() = default;

    virtual TSurface CreateSurface(GPUVAddr gpu_addr, const SurfaceParams& params) = 0;

    virtual void ImageCopy(TSurface& src_surface, TSurface& dst_surface,
                           const CopyParams& copy_params) = 0;

    virtual void ImageBlit(TView& src_view, TView& dst_view,
                           const Tegra::Engines::Fermi2D::Config& copy_config) = 0;

    // Depending on the backend, a buffer copy can be slow as it means deoptimizing the texture
    // and reading it from a sepparate buffer.
    virtual void BufferCopy(TSurface& src_surface, TSurface& dst_surface) = 0;

    void ManageRenderTargetUnregister(TSurface& surface) {
        auto& maxwell3d = system.GPU().Maxwell3D();
        const u32 index = surface->GetRenderTarget();
        if (index == DEPTH_RT) {
            maxwell3d.dirty.depth_buffer = true;
        } else {
            maxwell3d.dirty.render_target[index] = true;
        }
        maxwell3d.dirty.render_settings = true;
    }

    void Register(TSurface surface) {
        const GPUVAddr gpu_addr = surface->GetGpuAddr();
        const CacheAddr cache_ptr = ToCacheAddr(system.GPU().MemoryManager().GetPointer(gpu_addr));
        const std::size_t size = surface->GetSizeInBytes();
        const std::optional<VAddr> cpu_addr =
            system.GPU().MemoryManager().GpuToCpuAddress(gpu_addr);
        if (!cache_ptr || !cpu_addr) {
            LOG_CRITICAL(HW_GPU, "Failed to register surface with unmapped gpu_address 0x{:016x}",
                         gpu_addr);
            return;
        }
        const bool continuous = system.GPU().MemoryManager().IsBlockContinuous(gpu_addr, size);
        surface->MarkAsContinuous(continuous);
        surface->SetCacheAddr(cache_ptr);
        surface->SetCpuAddr(*cpu_addr);
        RegisterInnerCache(surface);
        surface->MarkAsRegistered(true);
        rasterizer.UpdatePagesCachedCount(*cpu_addr, size, 1);
    }

    void Unregister(TSurface surface, UnregisterReason reason) {
        if (guard_render_targets && surface->IsProtected()) {
            return;
        }
        if (!guard_render_targets && surface->IsRenderTarget()) {
            ManageRenderTargetUnregister(surface);
        }

        if (IsResolutionScannerEnabled()) {
            if (reason == UnregisterReason::Restructured) {
                UnmarkScanner(surface);
            }
        }
        const std::size_t size = surface->GetSizeInBytes();
        const VAddr cpu_addr = surface->GetCpuAddr();
        rasterizer.UpdatePagesCachedCount(cpu_addr, size, -1);
        UnregisterInnerCache(surface);
        surface->MarkAsRegistered(false);
        ReserveSurface(surface->GetSurfaceParams(), surface);
    }

    TSurface GetUncachedSurface(const GPUVAddr gpu_addr, const SurfaceParams& params) {
        if (const auto surface = TryGetReservedSurface(params); surface) {
            surface->SetGpuAddr(gpu_addr);
            return surface;
        }
        // No reserved surface available, create a new one and reserve it
        auto new_surface{CreateSurface(gpu_addr, params)};
        return new_surface;
    }

    std::pair<TSurface, TView> GetFermiSurface(
        const Tegra::Engines::Fermi2D::Regs::Surface& config) {
        SurfaceParams params = SurfaceParams::CreateForFermiCopySurface(config);
        const GPUVAddr gpu_addr = config.Address();
        return GetSurface(gpu_addr, params, true, false);
    }

    // Must be called by child's create surface
    void SignalCreatedSurface(TSurface& new_surface) {
        if (EnabledRescaling() && IsInRSDatabase(new_surface)) {
            new_surface->MarkAsRescaled(true);
        }
    }

    Core::System& system;

private:
    enum class RecycleStrategy : u32 {
        Ignore = 0,
        Flush = 1,
        BufferCopy = 3,
    };

    /**
     * `PickStrategy` takes care of selecting a proper strategy to deal with a texture recycle.
     * @param overlaps, the overlapping surfaces registered in the cache.
     * @param params, the paremeters on the new surface.
     * @param gpu_addr, the starting address of the new surface.
     * @param untopological, tells the recycler that the texture has no way to match the overlaps
     * due to topological reasons.
     **/
    RecycleStrategy PickStrategy(std::vector<TSurface>& overlaps, const SurfaceParams& params,
                                 const GPUVAddr gpu_addr, const MatchTopologyResult untopological) {
        if (Settings::values.use_accurate_gpu_emulation) {
            return RecycleStrategy::Flush;
        }
        // 3D Textures decision
        if (params.block_depth > 1 || params.target == SurfaceTarget::Texture3D) {
            return RecycleStrategy::Flush;
        }
        for (auto s : overlaps) {
            const auto& s_params = s->GetSurfaceParams();
            if (s_params.block_depth > 1 || s_params.target == SurfaceTarget::Texture3D) {
                return RecycleStrategy::Flush;
            }
        }
        // Untopological decision
        if (untopological == MatchTopologyResult::CompressUnmatch) {
            return RecycleStrategy::Flush;
        }
        if (untopological == MatchTopologyResult::FullMatch && !params.is_tiled) {
            return RecycleStrategy::Flush;
        }
        return RecycleStrategy::Ignore;
    }

    /**
     *  `RecycleSurface` es a method we use to decide what to do with textures we can't resolve in
     *the cache It has 2 implemented strategies: Ignore and Flush. Ignore just unregisters all the
     *overlaps and loads the new texture. Flush, flushes all the overlaps into memory and loads the
     *new surface from that data.
     * @param overlaps, the overlapping surfaces registered in the cache.
     * @param params, the paremeters on the new surface.
     * @param gpu_addr, the starting address of the new surface.
     * @param preserve_contents, tells if the new surface should be loaded from meory or left blank
     * @param untopological, tells the recycler that the texture has no way to match the overlaps
     * due to topological reasons.
     **/
    std::pair<TSurface, TView> RecycleSurface(std::vector<TSurface>& overlaps,
                                              const SurfaceParams& params, const GPUVAddr gpu_addr,
                                              const bool preserve_contents,
                                              const MatchTopologyResult untopological) {
        const bool do_load = preserve_contents && Settings::values.use_accurate_gpu_emulation;
        for (auto& surface : overlaps) {
            Unregister(surface, UnregisterReason::Recycled);
        }
        switch (PickStrategy(overlaps, params, gpu_addr, untopological)) {
        case RecycleStrategy::Ignore: {
            return InitializeSurface(gpu_addr, params, do_load);
        }
        case RecycleStrategy::Flush: {
            std::sort(overlaps.begin(), overlaps.end(),
                      [](const TSurface& a, const TSurface& b) -> bool {
                          return a->GetModificationTick() < b->GetModificationTick();
                      });
            for (auto& surface : overlaps) {
                FlushSurface(surface);
            }
            return InitializeSurface(gpu_addr, params, preserve_contents);
        }
        case RecycleStrategy::BufferCopy: {
            auto new_surface = GetUncachedSurface(gpu_addr, params);
            BufferCopy(overlaps[0], new_surface);
            return {new_surface, new_surface->GetMainView()};
        }
        default: {
            UNIMPLEMENTED_MSG("Unimplemented Texture Cache Recycling Strategy!");
            return InitializeSurface(gpu_addr, params, do_load);
        }
        }
    }

    /**
     * `RebuildSurface` this method takes a single surface and recreates into another that
     * may differ in format, target or width alingment.
     * @param current_surface, the registered surface in the cache which we want to convert.
     * @param params, the new surface params which we'll use to recreate the surface.
     **/
    std::pair<TSurface, TView> RebuildSurface(TSurface current_surface, const SurfaceParams& params,
                                              bool is_render) {
        const auto gpu_addr = current_surface->GetGpuAddr();
        const auto& cr_params = current_surface->GetSurfaceParams();
        TSurface new_surface;
        if (cr_params.pixel_format != params.pixel_format && !is_render &&
            GetSiblingFormat(cr_params.pixel_format) == params.pixel_format) {
            SurfaceParams new_params = params;
            new_params.pixel_format = cr_params.pixel_format;
            new_params.component_type = cr_params.component_type;
            new_params.type = cr_params.type;
            new_surface = GetUncachedSurface(gpu_addr, new_params);
        } else {
            new_surface = GetUncachedSurface(gpu_addr, params);
        }
        const auto& final_params = new_surface->GetSurfaceParams();
        if (cr_params.type != final_params.type ||
            (cr_params.component_type != final_params.component_type)) {
            BufferCopy(current_surface, new_surface);
        } else {
            std::vector<CopyParams> bricks = current_surface->BreakDown(final_params);
            for (auto& brick : bricks) {
                ImageCopy(current_surface, new_surface, brick);
            }
        }
        if (IsResolutionScannerEnabled()) {
            if (IsInRSDatabase(current_surface)) {
                if (IsRSBlacklisted(new_surface)) {
                    UnmarkScanner(current_surface);
                } else {
                    MarkScanner(new_surface);
                }
            } else if (IsInRSDatabase(new_surface)) {
                if (IsRSBlacklisted(current_surface)) {
                    UnmarkScanner(new_surface);
                } else {
                    MarkScanner(current_surface);
                }
            }
        }
        Unregister(current_surface, UnregisterReason::Rebuilt);
        Register(new_surface);
        new_surface->MarkAsModified(current_surface->IsModified(), Tick());
        return {new_surface, new_surface->GetMainView()};
    }

    /**
     * `ManageStructuralMatch` this method takes a single surface and checks with the new surface's
     * params if it's an exact match, we return the main view of the registered surface. If it's
     * formats don't match, we rebuild the surface. We call this last method a `Mirage`. If formats
     * match but the targets don't, we create an overview View of the registered surface.
     * @param current_surface, the registered surface in the cache which we want to convert.
     * @param params, the new surface params which we want to check.
     **/
    std::pair<TSurface, TView> ManageStructuralMatch(TSurface current_surface,
                                                     const SurfaceParams& params, bool is_render) {
        const bool is_mirage = !current_surface->MatchFormat(params.pixel_format);
        const bool matches_target = current_surface->MatchTarget(params.target);
        const auto match_check = [&]() -> std::pair<TSurface, TView> {
            if (matches_target) {
                return {current_surface, current_surface->GetMainView()};
            }
            return {current_surface, current_surface->EmplaceOverview(params)};
        };
        if (!is_mirage) {
            return match_check();
        }
        if (!is_render && GetSiblingFormat(current_surface->GetFormat()) == params.pixel_format) {
            return match_check();
        }
        return RebuildSurface(current_surface, params, is_render);
    }

    /**
     * `TryReconstructSurface` unlike `RebuildSurface` where we know the registered surface
     * matches the candidate in some way, we got no guarantess here. We try to see if the overlaps
     * are sublayers/mipmaps of the new surface, if they all match we end up recreating a surface
     * for them, else we return nothing.
     * @param overlaps, the overlapping surfaces registered in the cache.
     * @param params, the paremeters on the new surface.
     * @param gpu_addr, the starting address of the new surface.
     **/
    std::optional<std::pair<TSurface, TView>> TryReconstructSurface(std::vector<TSurface>& overlaps,
                                                                    const SurfaceParams& params,
                                                                    const GPUVAddr gpu_addr) {
        if (params.target == SurfaceTarget::Texture3D) {
            return {};
        }
        bool modified = false;
        TSurface new_surface = GetUncachedSurface(gpu_addr, params);
        u32 passed_tests = 0;
        for (auto& surface : overlaps) {
            const SurfaceParams& src_params = surface->GetSurfaceParams();
            if (src_params.is_layered || src_params.num_levels > 1) {
                // We send this cases to recycle as they are more complex to handle
                return {};
            }
            const std::size_t candidate_size = surface->GetSizeInBytes();
            auto mipmap_layer{new_surface->GetLayerMipmap(surface->GetGpuAddr())};
            if (!mipmap_layer) {
                continue;
            }
            const auto [layer, mipmap] = *mipmap_layer;
            if (new_surface->GetMipmapSize(mipmap) != candidate_size) {
                continue;
            }
            modified |= surface->IsModified();
            // Now we got all the data set up
            const u32 width = SurfaceParams::IntersectWidth(src_params, params, 0, mipmap);
            const u32 height = SurfaceParams::IntersectHeight(src_params, params, 0, mipmap);
            const CopyParams copy_params(0, 0, 0, 0, 0, layer, 0, mipmap, width, height, 1);
            passed_tests++;
            ImageCopy(surface, new_surface, copy_params);
        }
        if (passed_tests == 0) {
            return {};
            // In Accurate GPU all tests should pass, else we recycle
        } else if (Settings::values.use_accurate_gpu_emulation && passed_tests != overlaps.size()) {
            return {};
        }
        for (auto surface : overlaps) {
            Unregister(surface, UnregisterReason::Restructured);
        }
        new_surface->MarkAsModified(modified, Tick());
        Register(new_surface);
        return {{new_surface, new_surface->GetMainView()}};
    }

    /**
     * `GetSurface` gets the starting address and parameters of a candidate surface and tries
     * to find a matching surface within the cache. This is done in 3 big steps. The first is to
     * check the 1st Level Cache in order to find an exact match, if we fail, we move to step 2.
     * Step 2 is checking if there are any overlaps at all, if none, we just load the texture from
     * memory else we move to step 3. Step 3 consists on figuring the relationship between the
     * candidate texture and the overlaps. We divide the scenarios depending if there's 1 or many
     * overlaps. If there's many, we just try to reconstruct a new surface out of them based on the
     * candidate's parameters, if we fail, we recycle. When there's only 1 overlap then we have to
     * check if the candidate is a view (layer/mipmap) of the overlap or if the registered surface
     * is a mipmap/layer of the candidate. In this last case we reconstruct a new surface.
     * @param gpu_addr, the starting address of the candidate surface.
     * @param params, the paremeters on the candidate surface.
     * @param preserve_contents, tells if the new surface should be loaded from meory or left blank.
     **/
    std::pair<TSurface, TView> GetSurface(const GPUVAddr gpu_addr, const SurfaceParams& params,
                                          bool preserve_contents, bool is_render) {
        const auto host_ptr{system.GPU().MemoryManager().GetPointer(gpu_addr)};
        const auto cache_addr{ToCacheAddr(host_ptr)};

        // Step 0: guarantee a valid surface
        if (!cache_addr) {
            // Return a null surface if it's invalid
            SurfaceParams new_params = params;
            new_params.width = 1;
            new_params.height = 1;
            new_params.depth = 1;
            new_params.block_height = 0;
            new_params.block_depth = 0;
            return InitializeSurface(gpu_addr, new_params, false);
        }

        // Step 1
        // Check Level 1 Cache for a fast structural match. If candidate surface
        // matches at certain level we are pretty much done.
        if (const auto iter = l1_cache.find(cache_addr); iter != l1_cache.end()) {
            TSurface& current_surface = iter->second;
            const auto topological_result = current_surface->MatchesTopology(params);
            if (topological_result != MatchTopologyResult::FullMatch) {
                std::vector<TSurface> overlaps{current_surface};
                return RecycleSurface(overlaps, params, gpu_addr, preserve_contents,
                                      topological_result);
            }
            const auto struct_result = current_surface->MatchesStructure(params);
            if (struct_result != MatchStructureResult::None &&
                (params.target != SurfaceTarget::Texture3D ||
                 current_surface->MatchTarget(params.target))) {
                if (struct_result == MatchStructureResult::FullMatch) {
                    return ManageStructuralMatch(current_surface, params, is_render);
                } else {
                    return RebuildSurface(current_surface, params, is_render);
                }
            }
        }

        // Step 2
        // Obtain all possible overlaps in the memory region
        const std::size_t candidate_size = params.GetGuestSizeInBytes();
        auto overlaps{GetSurfacesInRegion(cache_addr, candidate_size)};

        // If none are found, we are done. we just load the surface and create it.
        if (overlaps.empty()) {
            return InitializeSurface(gpu_addr, params, preserve_contents);
        }

        // Step 3
        // Now we need to figure the relationship between the texture and its overlaps
        // we do a topological test to ensure we can find some relationship. If it fails
        // inmediatly recycle the texture
        for (const auto& surface : overlaps) {
            const auto topological_result = surface->MatchesTopology(params);
            if (topological_result != MatchTopologyResult::FullMatch) {
                return RecycleSurface(overlaps, params, gpu_addr, preserve_contents,
                                      topological_result);
            }
        }

        // Split cases between 1 overlap or many.
        if (overlaps.size() == 1) {
            TSurface current_surface = overlaps[0];
            // First check if the surface is within the overlap. If not, it means
            // two things either the candidate surface is a supertexture of the overlap
            // or they don't match in any known way.
            if (!current_surface->IsInside(gpu_addr, gpu_addr + candidate_size)) {
                if (current_surface->GetGpuAddr() == gpu_addr) {
                    std::optional<std::pair<TSurface, TView>> view =
                        TryReconstructSurface(overlaps, params, gpu_addr);
                    if (view) {
                        return *view;
                    }
                }
                return RecycleSurface(overlaps, params, gpu_addr, preserve_contents,
                                      MatchTopologyResult::FullMatch);
            }
            // Now we check if the candidate is a mipmap/layer of the overlap
            std::optional<TView> view =
                current_surface->EmplaceView(params, gpu_addr, candidate_size);
            if (view) {
                const bool is_mirage = !current_surface->MatchFormat(params.pixel_format);
                if (is_mirage) {
                    // On a mirage view, we need to recreate the surface under this new view
                    // and then obtain a view again.
                    SurfaceParams new_params = current_surface->GetSurfaceParams();
                    const u32 wh = SurfaceParams::ConvertWidth(
                        new_params.width, new_params.pixel_format, params.pixel_format);
                    const u32 hh = SurfaceParams::ConvertHeight(
                        new_params.height, new_params.pixel_format, params.pixel_format);
                    new_params.width = wh;
                    new_params.height = hh;
                    new_params.pixel_format = params.pixel_format;
                    std::pair<TSurface, TView> pair =
                        RebuildSurface(current_surface, new_params, is_render);
                    std::optional<TView> mirage_view =
                        pair.first->EmplaceView(params, gpu_addr, candidate_size);
                    if (mirage_view)
                        return {pair.first, *mirage_view};
                    return RecycleSurface(overlaps, params, gpu_addr, preserve_contents,
                                          MatchTopologyResult::FullMatch);
                }
                return {current_surface, *view};
            }
        } else {
            // If there are many overlaps, odds are they are subtextures of the candidate
            // surface. We try to construct a new surface based on the candidate parameters,
            // using the overlaps. If a single overlap fails, this will fail.
            std::optional<std::pair<TSurface, TView>> view =
                TryReconstructSurface(overlaps, params, gpu_addr);
            if (view) {
                return *view;
            }
        }
        // We failed all the tests, recycle the overlaps into a new texture.
        return RecycleSurface(overlaps, params, gpu_addr, preserve_contents,
                              MatchTopologyResult::FullMatch);
    }

    std::pair<TSurface, TView> InitializeSurface(GPUVAddr gpu_addr, const SurfaceParams& params,
                                                 bool preserve_contents) {
        auto new_surface{GetUncachedSurface(gpu_addr, params)};
        Register(new_surface);
        if (preserve_contents) {
            LoadSurface(new_surface);
        }
        return {new_surface, new_surface->GetMainView()};
    }

    void LoadSurfaceRescaled(TSurface& surface) {
        const auto& params = surface->GetSurfaceParams();
        enable_resolution_scaling = false;
        TSurface proxy = CreateSurface(surface->GetGpuAddr(), params);
        enable_resolution_scaling = true;
        staging_cache.GetBuffer(0).resize(proxy->GetHostSizeInBytes());
        proxy->LoadBuffer(system.GPU().MemoryManager(), staging_cache);
        proxy->UploadTexture(staging_cache.GetBuffer(0));
        Tegra::Engines::Fermi2D::Config copy_config;
        const Common::Rectangle<u32> rect{0, 0, params.width, params.height};
        copy_config.operation = Tegra::Engines::Fermi2D::Operation::SrcCopy;
        copy_config.filter = Tegra::Engines::Fermi2D::Filter::Linear;
        copy_config.src_rect = rect;
        copy_config.dst_rect = rect;
        TView src_view = proxy->GetMainView();
        TView dst_view = surface->GetMainView();
        ImageBlit(src_view, dst_view, copy_config);
    }

    void LoadSurface(TSurface& surface) {
        if (surface->IsRescaled()) {
            LoadSurfaceRescaled(surface);
            return;
        }
        staging_cache.GetBuffer(0).resize(surface->GetHostSizeInBytes());
        surface->LoadBuffer(system.GPU().MemoryManager(), staging_cache);
        surface->UploadTexture(staging_cache.GetBuffer(0));
        surface->MarkAsModified(false, Tick());
    }

    void FlushSurface(const TSurface& surface) {
        if (!surface->IsModified()) {
            return;
        }
        if (IsResolutionScannerEnabled()) {
            UnmarkScanner(surface);
        }
        staging_cache.GetBuffer(0).resize(surface->GetHostSizeInBytes());
        surface->DownloadTexture(staging_cache.GetBuffer(0));
        surface->FlushBuffer(system.GPU().MemoryManager(), staging_cache);
        surface->MarkAsModified(false, Tick());
    }

    void RegisterInnerCache(TSurface& surface) {
        const CacheAddr cache_addr = surface->GetCacheAddr();
        CacheAddr start = cache_addr >> registry_page_bits;
        const CacheAddr end = (surface->GetCacheAddrEnd() - 1) >> registry_page_bits;
        l1_cache[cache_addr] = surface;
        while (start <= end) {
            registry[start].push_back(surface);
            start++;
        }
    }

    void UnregisterInnerCache(TSurface& surface) {
        const CacheAddr cache_addr = surface->GetCacheAddr();
        CacheAddr start = cache_addr >> registry_page_bits;
        const CacheAddr end = (surface->GetCacheAddrEnd() - 1) >> registry_page_bits;
        l1_cache.erase(cache_addr);
        while (start <= end) {
            auto& reg{registry[start]};
            reg.erase(std::find(reg.begin(), reg.end(), surface));
            start++;
        }
    }

    std::vector<TSurface> GetSurfacesInRegion(const CacheAddr cache_addr, const std::size_t size) {
        if (size == 0) {
            return {};
        }
        const CacheAddr cache_addr_end = cache_addr + size;
        CacheAddr start = cache_addr >> registry_page_bits;
        const CacheAddr end = (cache_addr_end - 1) >> registry_page_bits;
        std::vector<TSurface> surfaces;
        while (start <= end) {
            std::vector<TSurface>& list = registry[start];
            for (auto& surface : list) {
                if (!surface->IsPicked() && surface->Overlaps(cache_addr, cache_addr_end)) {
                    surface->MarkAsPicked(true);
                    surfaces.push_back(surface);
                }
            }
            start++;
        }
        for (auto& surface : surfaces) {
            surface->MarkAsPicked(false);
        }
        return surfaces;
    }

    void ReserveSurface(const SurfaceParams& params, TSurface surface) {
        surface_reserve[params].push_back(std::move(surface));
    }

    TSurface TryGetReservedSurface(const SurfaceParams& params) {
        auto search{surface_reserve.find(params)};
        if (search == surface_reserve.end()) {
            return {};
        }
        for (auto& surface : search->second) {
            if (!surface->IsRegistered()) {
                return surface;
            }
        }
        return {};
    }

    bool EnabledRescaling() const {
        return enable_resolution_scaling;
    }

    bool IsResolutionScannerEnabled() const {
        return Settings::values.use_resolution_scanner;
    }

    void UnmarkScanner(const TSurface& surface) {
        const auto params = surface->GetSurfaceParams();
        scaling_database.Unregister(params.pixel_format, params.width, params.height);
    }

    void MarkScanner(const TSurface& surface) {
        const auto params = surface->GetSurfaceParams();
        if (params.target != SurfaceTarget::Texture2D || params.num_levels > 1 ||
            params.IsCompressed() || params.block_depth > 1) {
            return;
        }
        scaling_database.Register(params.pixel_format, params.width, params.height);
    }

    bool IsRSBlacklisted(const TSurface& surface) const {
        const auto params = surface->GetSurfaceParams();
        return scaling_database.IsBlacklisted(params.pixel_format, params.width, params.height);
    }

    bool IsInRSDatabase(const TSurface& surface) const {
        const auto& params = surface->GetSurfaceParams();
        return scaling_database.IsInDatabase(params.pixel_format, params.width, params.height);
    }

    bool CheckBlackListMatch() {
        u32 enabled_targets = 0;
        u32 black_listed = 0;
        bool black_list = false;
        for (const auto& target : render_targets) {
            if (target.target) {
                enabled_targets++;
                if (IsRSBlacklisted(target.target)) {
                    black_list = true;
                    black_listed++;
                }
            }
        }
        if (depth_buffer.target) {
            enabled_targets++;
            if (IsRSBlacklisted(depth_buffer.target)) {
                black_list = true;
                black_listed++;
            }
        }
        if (black_list) {
            if (black_listed != enabled_targets) {
                std::string blacklist_msg{};
                for (const auto& target : render_targets) {
                    if (target.target) {
                        UnmarkScanner(target.target);
                        const auto& params = target.target->GetSurfaceParams();
                        blacklist_msg += fmt::format("Format:{}, Height:{}, Width:{}\n",
                                                     static_cast<u32>(params.pixel_format),
                                                     params.height, params.width);
                    }
                }
                if (depth_buffer.target) {
                    UnmarkScanner(depth_buffer.target);
                    const auto& params = depth_buffer.target->GetSurfaceParams();
                    blacklist_msg += fmt::format("Format:{}, Height:{}, Width:{}\n",
                                                 static_cast<u32>(params.pixel_format),
                                                 params.height, params.width);
                }
                LOG_CRITICAL(HW_GPU, "Scan detected a conflict:\n{}\nBlacklisting all",
                             blacklist_msg);
            }
        }
        return false;
    }

    bool CheckResolutionScalingEnabled() {
        u32 enabled_targets = 0;
        u32 rescaled_targets = 0;
        bool rescaling = false;
        for (const auto& target : render_targets) {
            if (target.target) {
                enabled_targets++;
                if (target.target->IsRescaled()) {
                    rescaling = true;
                    rescaled_targets++;
                }
            }
        }
        if (depth_buffer.target) {
            enabled_targets++;
            if (depth_buffer.target->IsRescaled()) {
                rescaling = true;
                rescaled_targets++;
            }
        }
        if (rescaling) {
            if (rescaled_targets != enabled_targets) {
                LOG_CRITICAL(HW_GPU,
                             "Rescaling Database is incorrectly set! Rescan the database!.");
                return false;
            }
            return true;
        }
        return false;
    }

    constexpr PixelFormat GetSiblingFormat(PixelFormat format) const {
        return siblings_table[static_cast<std::size_t>(format)];
    }

    struct FramebufferTargetInfo {
        TSurface target;
        TView view;
    };

    VideoCore::RasterizerInterface& rasterizer;

    u64 ticks{};

    // Guards the cache for protection conflicts.
    bool guard_render_targets{};
    bool guard_samplers{};
    bool enable_resolution_scaling{};

    // The siblings table is for formats that can inter exchange with one another
    // without causing issues. This is only valid when a conflict occurs on a non
    // rendering use.
    std::array<PixelFormat, static_cast<std::size_t>(PixelFormat::Max)> siblings_table;

    // The internal Cache is different for the Texture Cache. It's based on buckets
    // of 1MB. This fits better for the purpose of this cache as textures are normaly
    // large in size.
    static constexpr u64 registry_page_bits{20};
    static constexpr u64 registry_page_size{1 << registry_page_bits};
    std::unordered_map<CacheAddr, std::vector<TSurface>> registry;

    static constexpr u32 DEPTH_RT = 8;
    static constexpr u32 NO_RT = 0xFFFFFFFF;

    // The L1 Cache is used for fast texture lookup before checking the overlaps
    // This avoids calculating size and other stuffs.
    std::unordered_map<CacheAddr, TSurface> l1_cache;

    /// The surface reserve is a "backup" cache, this is where we put unique surfaces that have
    /// previously been used. This is to prevent surfaces from being constantly created and
    /// destroyed when used with different surface parameters.
    std::unordered_map<SurfaceParams, std::vector<TSurface>> surface_reserve;
    std::array<FramebufferTargetInfo, Tegra::Engines::Maxwell3D::Regs::NumRenderTargets>
        render_targets;
    FramebufferTargetInfo depth_buffer;

    std::vector<TSurface> sampled_textures;

    StagingCache staging_cache;
    std::recursive_mutex mutex;

    Resolution::ScalingDatabase scaling_database;
};

} // namespace VideoCommon
