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

public:
    void InvalidateRegion(CacheAddr addr, std::size_t size) {
        std::lock_guard lock{mutex};

        for (const auto& surface : GetSurfacesInRegion(addr, size)) {
            Unregister(surface);
        }
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
        if (depth_buffer.target)
            depth_buffer.target->MarkAsRenderTarget(true, DEPTH_RT);
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
        if (render_targets[index].target)
            render_targets[index].target->MarkAsRenderTarget(true, static_cast<u32>(index));
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
        SurfaceParams src_params = SurfaceParams::CreateForFermiCopySurface(src_config);
        SurfaceParams dst_params = SurfaceParams::CreateForFermiCopySurface(dst_config);
        const GPUVAddr src_gpu_addr = src_config.Address();
        const GPUVAddr dst_gpu_addr = dst_config.Address();
        DeduceBestBlit(src_params, dst_params, src_gpu_addr, dst_gpu_addr);
        std::pair<TSurface, TView> dst_surface = GetSurface(dst_gpu_addr, dst_params, true, false);
        std::pair<TSurface, TView> src_surface = GetSurface(src_gpu_addr, src_params, true, false);
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

protected:
    TextureCache(Core::System& system, VideoCore::RasterizerInterface& rasterizer)
        : system{system}, rasterizer{rasterizer} {
        for (std::size_t i = 0; i < Tegra::Engines::Maxwell3D::Regs::NumRenderTargets; i++) {
            SetEmptyColorBuffer(i);
        }

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

    void Unregister(TSurface surface) {
        if (guard_render_targets && surface->IsProtected()) {
            return;
        }
        if (!guard_render_targets && surface->IsRenderTarget()) {
            ManageRenderTargetUnregister(surface);
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

    Core::System& system;

private:
    enum class RecycleStrategy : u32 {
        Ignore = 0,
        Flush = 1,
        BufferCopy = 3,
    };

    enum class DeductionType : u32 {
        DeductionComplete,
        DeductionIncomplete,
        DeductionFailed,
    };

    struct Deduction {
        DeductionType type{DeductionType::DeductionFailed};
        TSurface surface{};

        bool Failed() const {
            return type == DeductionType::DeductionFailed;
        }

        bool Incomplete() const {
            return type == DeductionType::DeductionIncomplete;
        }

        bool IsDepth() const {
            return surface->GetSurfaceParams().IsPixelFormatZeta();
        }
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
            Unregister(surface);
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
        Unregister(current_surface);
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
            Unregister(surface);
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

    /**
     * `DeduceSurface` gets the starting address and parameters of a candidate surface and tries
     * to find a matching surface within the cache that's similar to it. If there are many textures
     * or the texture found if entirely incompatible, it will fail. If no texture is found, the
     * blit will be unsuccessful.
     * @param gpu_addr, the starting address of the candidate surface.
     * @param params, the paremeters on the candidate surface.
     **/
    Deduction DeduceSurface(const GPUVAddr gpu_addr, const SurfaceParams& params) {
        const auto host_ptr{system.GPU().MemoryManager().GetPointer(gpu_addr)};
        const auto cache_addr{ToCacheAddr(host_ptr)};

        if (!cache_addr) {
            Deduction result{};
            result.type = DeductionType::DeductionFailed;
            return result;
        }

        if (const auto iter = l1_cache.find(cache_addr); iter != l1_cache.end()) {
            TSurface& current_surface = iter->second;
            const auto topological_result = current_surface->MatchesTopology(params);
            if (topological_result != MatchTopologyResult::FullMatch) {
                Deduction result{};
                result.type = DeductionType::DeductionFailed;
                return result;
            }
            const auto struct_result = current_surface->MatchesStructure(params);
            if (struct_result != MatchStructureResult::None &&
                current_surface->MatchTarget(params.target)) {
                Deduction result{};
                result.type = DeductionType::DeductionComplete;
                result.surface = current_surface;
                return result;
            }
        }

        const std::size_t candidate_size = params.GetGuestSizeInBytes();
        auto overlaps{GetSurfacesInRegion(cache_addr, candidate_size)};

        if (overlaps.empty()) {
            Deduction result{};
            result.type = DeductionType::DeductionIncomplete;
            return result;
        }

        if (overlaps.size() > 1) {
            Deduction result{};
            result.type = DeductionType::DeductionFailed;
            return result;
        } else {
            Deduction result{};
            result.type = DeductionType::DeductionComplete;
            result.surface = overlaps[0];
            return result;
        }
    }

    /**
     * `DeduceBestBlit` gets the a source and destination starting address and parameters,
     * and tries to deduce if they are supposed to be depth textures. If so, their
     * parameters are modified and fixed into so.
     * @param gpu_addr, the starting address of the candidate surface.
     * @param params, the parameters on the candidate surface.
     **/
    void DeduceBestBlit(SurfaceParams& src_params, SurfaceParams& dst_params,
                        const GPUVAddr src_gpu_addr, const GPUVAddr dst_gpu_addr) {
        auto deduc_src = DeduceSurface(src_gpu_addr, src_params);
        auto deduc_dst = DeduceSurface(src_gpu_addr, src_params);
        if (deduc_src.Failed() || deduc_dst.Failed()) {
            return;
        }

        const bool incomplete_src = deduc_src.Incomplete();
        const bool incomplete_dst = deduc_dst.Incomplete();

        if (incomplete_src && incomplete_dst) {
            return;
        }

        const bool any_incomplete = incomplete_src || incomplete_dst;

        if (!any_incomplete && !(deduc_src.IsDepth() && deduc_dst.IsDepth())) {
            return;
        }

        if (incomplete_src && !(deduc_dst.IsDepth())) {
            return;
        }

        if (incomplete_dst && !(deduc_src.IsDepth())) {
            return;
        }

        const auto inherit_format = ([](SurfaceParams& to, TSurface from) {
            const SurfaceParams& params = from->GetSurfaceParams();
            to.pixel_format = params.pixel_format;
            to.component_type = params.component_type;
            to.type = params.type;
        });
        // Now we got the cases where one or both is Depth and the other is not known
        if (!incomplete_src) {
            inherit_format(src_params, deduc_src.surface);
        } else {
            inherit_format(src_params, deduc_dst.surface);
        }
        if (!incomplete_dst) {
            inherit_format(dst_params, deduc_dst.surface);
        } else {
            inherit_format(dst_params, deduc_src.surface);
        }
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

    void LoadSurface(const TSurface& surface) {
        staging_cache.GetBuffer(0).resize(surface->GetHostSizeInBytes());
        surface->LoadBuffer(system.GPU().MemoryManager(), staging_cache);
        surface->UploadTexture(staging_cache.GetBuffer(0));
        surface->MarkAsModified(false, Tick());
    }

    void FlushSurface(const TSurface& surface) {
        if (!surface->IsModified()) {
            return;
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
};

} // namespace VideoCommon
