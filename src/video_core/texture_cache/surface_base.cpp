// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/common_types.h"
#include "common/microprofile.h"
#include "video_core/memory_manager.h"
#include "video_core/texture_cache/surface_base.h"
#include "video_core/texture_cache/surface_params.h"
#include "video_core/textures/convert.h"

namespace VideoCommon {

MICROPROFILE_DEFINE(GPU_Load_Texture, "GPU", "Texture Load", MP_RGB(128, 192, 128));
MICROPROFILE_DEFINE(GPU_Flush_Texture, "GPU", "Texture Flush", MP_RGB(128, 192, 128));

using Tegra::Texture::ConvertFromGuestToHost;
using VideoCore::MortonSwizzleMode;
using VideoCore::Surface::SurfaceCompression;

StagingCache::StagingCache() = default;

StagingCache::~StagingCache() = default;

SurfaceBaseImpl::SurfaceBaseImpl(GPUVAddr gpu_addr, const SurfaceParams& params)
    : params{params}, mipmap_sizes(params.num_levels),
      mipmap_offsets(params.num_levels), gpu_addr{gpu_addr}, host_memory_size{
                                                                 params.GetHostSizeInBytes()} {
    std::size_t offset = 0;
    for (u32 level = 0; level < params.num_levels; ++level) {
        const std::size_t mipmap_size{params.GetGuestMipmapSize(level)};
        mipmap_sizes[level] = mipmap_size;
        mipmap_offsets[level] = offset;
        offset += mipmap_size;
    }
    layer_size = offset;
    if (params.is_layered) {
        if (params.is_tiled) {
            layer_size =
                SurfaceParams::AlignLayered(layer_size, params.block_height, params.block_depth);
        }
        guest_memory_size = layer_size * params.depth;
    } else {
        guest_memory_size = layer_size;
    }
}

MatchTopologyResult SurfaceBaseImpl::MatchesTopology(const SurfaceParams& rhs) const {
    const u32 src_bpp{params.GetBytesPerPixel()};
    const u32 dst_bpp{rhs.GetBytesPerPixel()};
    const bool ib1 = params.IsBuffer();
    const bool ib2 = rhs.IsBuffer();
    if (std::tie(src_bpp, params.is_tiled, ib1) == std::tie(dst_bpp, rhs.is_tiled, ib2)) {
        const bool cb1 = params.IsCompressed();
        const bool cb2 = rhs.IsCompressed();
        if (cb1 == cb2) {
            return MatchTopologyResult::FullMatch;
        }
        return MatchTopologyResult::CompressUnmatch;
    }
    return MatchTopologyResult::None;
}

MatchStructureResult SurfaceBaseImpl::MatchesStructure(const SurfaceParams& rhs) const {
    // Buffer surface Check
    if (params.IsBuffer()) {
        const std::size_t wd1 = params.width * params.GetBytesPerPixel();
        const std::size_t wd2 = rhs.width * rhs.GetBytesPerPixel();
        if (wd1 == wd2) {
            return MatchStructureResult::FullMatch;
        }
        return MatchStructureResult::None;
    }

    // Linear Surface check
    if (!params.is_tiled) {
        if (std::tie(params.width, params.height, params.pitch) ==
            std::tie(rhs.width, rhs.height, rhs.pitch)) {
            return MatchStructureResult::FullMatch;
        }
        return MatchStructureResult::None;
    }

    // Tiled Surface check
    if (std::tie(params.depth, params.block_width, params.block_height, params.block_depth,
                 params.tile_width_spacing, params.num_levels) ==
        std::tie(rhs.depth, rhs.block_width, rhs.block_height, rhs.block_depth,
                 rhs.tile_width_spacing, rhs.num_levels)) {
        if (std::tie(params.width, params.height) == std::tie(rhs.width, rhs.height)) {
            return MatchStructureResult::FullMatch;
        }
        const u32 ws = SurfaceParams::ConvertWidth(rhs.GetBlockAlignedWidth(), params.pixel_format,
                                                   rhs.pixel_format);
        const u32 hs =
            SurfaceParams::ConvertHeight(rhs.height, params.pixel_format, rhs.pixel_format);
        const u32 w1 = params.GetBlockAlignedWidth();
        if (std::tie(w1, params.height) == std::tie(ws, hs)) {
            return MatchStructureResult::SemiMatch;
        }
    }
    return MatchStructureResult::None;
}

std::optional<std::pair<u32, u32>> SurfaceBaseImpl::GetLayerMipmap(
    const GPUVAddr candidate_gpu_addr) const {
    if (gpu_addr == candidate_gpu_addr) {
        return {{0, 0}};
    }
    if (candidate_gpu_addr < gpu_addr) {
        return {};
    }
    const auto relative_address{static_cast<GPUVAddr>(candidate_gpu_addr - gpu_addr)};
    const auto layer{static_cast<u32>(relative_address / layer_size)};
    const GPUVAddr mipmap_address = relative_address - layer_size * layer;
    const auto mipmap_it =
        Common::BinaryFind(mipmap_offsets.begin(), mipmap_offsets.end(), mipmap_address);
    if (mipmap_it == mipmap_offsets.end()) {
        return {};
    }
    const auto level{static_cast<u32>(std::distance(mipmap_offsets.begin(), mipmap_it))};
    return std::make_pair(layer, level);
}

std::vector<CopyParams> SurfaceBaseImpl::BreakDownLayered(const SurfaceParams& in_params) const {
    const u32 layers{params.depth};
    const u32 mipmaps{params.num_levels};
    std::vector<CopyParams> result;
    result.reserve(static_cast<std::size_t>(layers) * static_cast<std::size_t>(mipmaps));

    for (u32 layer = 0; layer < layers; layer++) {
        for (u32 level = 0; level < mipmaps; level++) {
            const u32 width = SurfaceParams::IntersectWidth(params, in_params, level, level);
            const u32 height = SurfaceParams::IntersectHeight(params, in_params, level, level);
            result.emplace_back(width, height, layer, level);
        }
    }
    return result;
}

std::vector<CopyParams> SurfaceBaseImpl::BreakDownNonLayered(const SurfaceParams& in_params) const {
    const u32 mipmaps{params.num_levels};
    std::vector<CopyParams> result;
    result.reserve(mipmaps);

    for (u32 level = 0; level < mipmaps; level++) {
        const u32 width = SurfaceParams::IntersectWidth(params, in_params, level, level);
        const u32 height = SurfaceParams::IntersectHeight(params, in_params, level, level);
        const u32 depth{std::min(params.GetMipDepth(level), in_params.GetMipDepth(level))};
        result.emplace_back(width, height, depth, level);
    }
    return result;
}

void SurfaceBaseImpl::SwizzleFunc(MortonSwizzleMode mode, u8* memory, const SurfaceParams& params,
                                  u8* buffer, u32 level) {
    const u32 width{params.GetMipWidth(level)};
    const u32 height{params.GetMipHeight(level)};
    const u32 block_height{params.GetMipBlockHeight(level)};
    const u32 block_depth{params.GetMipBlockDepth(level)};

    std::size_t guest_offset{mipmap_offsets[level]};
    if (params.is_layered) {
        std::size_t host_offset{0};
        const std::size_t guest_stride = layer_size;
        const std::size_t host_stride = params.GetHostLayerSize(level);
        for (u32 layer = 0; layer < params.depth; ++layer) {
            MortonSwizzle(mode, params.pixel_format, width, block_height, height, block_depth, 1,
                          params.tile_width_spacing, buffer + host_offset, memory + guest_offset);
            guest_offset += guest_stride;
            host_offset += host_stride;
        }
    } else {
        MortonSwizzle(mode, params.pixel_format, width, block_height, height, block_depth,
                      params.GetMipDepth(level), params.tile_width_spacing, buffer,
                      memory + guest_offset);
    }
}

void SurfaceBaseImpl::LoadBuffer(Tegra::MemoryManager& memory_manager,
                                 StagingCache& staging_cache) {
    MICROPROFILE_SCOPE(GPU_Load_Texture);
    auto& staging_buffer = staging_cache.GetBuffer(0);
    u8* host_ptr;
    is_continuous = memory_manager.IsBlockContinuous(gpu_addr, guest_memory_size);

    // Handle continuouty
    if (is_continuous) {
        // Use physical memory directly
        host_ptr = memory_manager.GetPointer(gpu_addr);
        if (!host_ptr) {
            return;
        }
    } else {
        // Use an extra temporal buffer
        auto& tmp_buffer = staging_cache.GetBuffer(1);
        tmp_buffer.resize(guest_memory_size);
        host_ptr = tmp_buffer.data();
        memory_manager.ReadBlockUnsafe(gpu_addr, host_ptr, guest_memory_size);
    }

    if (params.is_tiled) {
        ASSERT_MSG(params.block_width == 0, "Block width is defined as {} on texture target {}",
                   params.block_width, static_cast<u32>(params.target));
        for (u32 level = 0; level < params.num_levels; ++level) {
            const std::size_t host_offset{params.GetHostMipmapLevelOffset(level)};
            SwizzleFunc(MortonSwizzleMode::MortonToLinear, host_ptr, params,
                        staging_buffer.data() + host_offset, level);
        }
    } else {
        ASSERT_MSG(params.num_levels == 1, "Linear mipmap loading is not implemented");
        const u32 bpp{params.GetBytesPerPixel()};
        const u32 block_width{params.GetDefaultBlockWidth()};
        const u32 block_height{params.GetDefaultBlockHeight()};
        const u32 width{(params.width + block_width - 1) / block_width};
        const u32 height{(params.height + block_height - 1) / block_height};
        const u32 copy_size{width * bpp};
        if (params.pitch == copy_size) {
            std::memcpy(staging_buffer.data(), host_ptr, params.GetHostSizeInBytes());
        } else {
            const u8* start{host_ptr};
            u8* write_to{staging_buffer.data()};
            for (u32 h = height; h > 0; --h) {
                std::memcpy(write_to, start, copy_size);
                start += params.pitch;
                write_to += copy_size;
            }
        }
    }

    auto compression_type = params.GetCompressionType();
    if (compression_type == SurfaceCompression::None ||
        compression_type == SurfaceCompression::Compressed)
        return;

    for (u32 level_up = params.num_levels; level_up > 0; --level_up) {
        const u32 level = level_up - 1;
        const std::size_t in_host_offset{params.GetHostMipmapLevelOffset(level)};
        const std::size_t out_host_offset = compression_type == SurfaceCompression::Rearranged
                                                ? in_host_offset
                                                : params.GetConvertedMipmapOffset(level);
        u8* in_buffer = staging_buffer.data() + in_host_offset;
        u8* out_buffer = staging_buffer.data() + out_host_offset;
        ConvertFromGuestToHost(in_buffer, out_buffer, params.pixel_format,
                               params.GetMipWidth(level), params.GetMipHeight(level),
                               params.GetMipDepth(level), true, true);
    }
}

void SurfaceBaseImpl::FlushBuffer(Tegra::MemoryManager& memory_manager,
                                  StagingCache& staging_cache) {
    MICROPROFILE_SCOPE(GPU_Flush_Texture);
    auto& staging_buffer = staging_cache.GetBuffer(0);
    u8* host_ptr;

    // Handle continuouty
    if (is_continuous) {
        // Use physical memory directly
        host_ptr = memory_manager.GetPointer(gpu_addr);
        if (!host_ptr) {
            return;
        }
    } else {
        // Use an extra temporal buffer
        auto& tmp_buffer = staging_cache.GetBuffer(1);
        tmp_buffer.resize(guest_memory_size);
        host_ptr = tmp_buffer.data();
    }

    if (params.is_tiled) {
        ASSERT_MSG(params.block_width == 0, "Block width is defined as {}", params.block_width);
        for (u32 level = 0; level < params.num_levels; ++level) {
            const std::size_t host_offset{params.GetHostMipmapLevelOffset(level)};
            SwizzleFunc(MortonSwizzleMode::LinearToMorton, host_ptr, params,
                        staging_buffer.data() + host_offset, level);
        }
    } else {
        ASSERT(params.target == SurfaceTarget::Texture2D);
        ASSERT(params.num_levels == 1);

        const u32 bpp{params.GetBytesPerPixel()};
        const u32 copy_size{params.width * bpp};
        if (params.pitch == copy_size) {
            std::memcpy(host_ptr, staging_buffer.data(), guest_memory_size);
        } else {
            u8* start{host_ptr};
            const u8* read_to{staging_buffer.data()};
            for (u32 h = params.height; h > 0; --h) {
                std::memcpy(start, read_to, copy_size);
                start += params.pitch;
                read_to += copy_size;
            }
        }
    }
    if (!is_continuous) {
        memory_manager.WriteBlockUnsafe(gpu_addr, host_ptr, guest_memory_size);
    }
}

} // namespace VideoCommon
