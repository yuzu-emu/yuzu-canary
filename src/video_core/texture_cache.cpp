// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/alignment.h"
#include "common/assert.h"
#include "common/cityhash.h"
#include "common/common_types.h"
#include "core/core.h"
#include "video_core/surface.h"
#include "video_core/texture_cache.h"
#include "video_core/textures/decoders.h"
#include "video_core/textures/texture.h"

namespace VideoCommon {

using VideoCore::Surface::SurfaceTarget;

using VideoCore::Surface::ComponentTypeFromDepthFormat;
using VideoCore::Surface::ComponentTypeFromRenderTarget;
using VideoCore::Surface::ComponentTypeFromTexture;
using VideoCore::Surface::PixelFormatFromDepthFormat;
using VideoCore::Surface::PixelFormatFromRenderTargetFormat;
using VideoCore::Surface::PixelFormatFromTextureFormat;
using VideoCore::Surface::SurfaceTargetFromTextureType;

constexpr u32 GetMipmapSize(bool uncompressed, u32 mip_size, u32 tile) {
    return uncompressed ? mip_size : std::max(1U, (mip_size + tile - 1) / tile);
}

SurfaceParams SurfaceParams::CreateForTexture(Core::System& system,
                                              const Tegra::Texture::FullTextureInfo& config) {
    SurfaceParams params;
    params.is_tiled = config.tic.IsTiled();
    params.block_width = params.is_tiled ? config.tic.BlockWidth() : 0,
    params.block_height = params.is_tiled ? config.tic.BlockHeight() : 0,
    params.block_depth = params.is_tiled ? config.tic.BlockDepth() : 0,
    params.tile_width_spacing = params.is_tiled ? (1 << config.tic.tile_width_spacing.Value()) : 1;
    params.pixel_format =
        PixelFormatFromTextureFormat(config.tic.format, config.tic.r_type.Value(), false);
    params.component_type = ComponentTypeFromTexture(config.tic.r_type.Value());
    params.type = GetFormatType(params.pixel_format);
    params.target = SurfaceTargetFromTextureType(config.tic.texture_type);
    params.width = Common::AlignUp(config.tic.Width(), GetCompressionFactor(params.pixel_format));
    params.height = Common::AlignUp(config.tic.Height(), GetCompressionFactor(params.pixel_format));
    params.depth = config.tic.Depth();
    if (params.target == SurfaceTarget::TextureCubemap ||
        params.target == SurfaceTarget::TextureCubeArray) {
        params.depth *= 6;
    }
    params.pitch = params.is_tiled ? 0 : config.tic.Pitch();
    params.unaligned_height = config.tic.Height();
    params.num_levels = config.tic.max_mip_level + 1;

    params.CalculateCachedValues();
    return params;
}

SurfaceParams SurfaceParams::CreateForDepthBuffer(
    Core::System& system, u32 zeta_width, u32 zeta_height, Tegra::DepthFormat format,
    u32 block_width, u32 block_height, u32 block_depth,
    Tegra::Engines::Maxwell3D::Regs::InvMemoryLayout type) {
    SurfaceParams params;
    params.is_tiled = type == Tegra::Engines::Maxwell3D::Regs::InvMemoryLayout::BlockLinear;
    params.block_width = 1 << std::min(block_width, 5U);
    params.block_height = 1 << std::min(block_height, 5U);
    params.block_depth = 1 << std::min(block_depth, 5U);
    params.tile_width_spacing = 1;
    params.pixel_format = PixelFormatFromDepthFormat(format);
    params.component_type = ComponentTypeFromDepthFormat(format);
    params.type = GetFormatType(params.pixel_format);
    params.width = zeta_width;
    params.height = zeta_height;
    params.unaligned_height = zeta_height;
    params.target = SurfaceTarget::Texture2D;
    params.depth = 1;
    params.num_levels = 1;

    params.CalculateCachedValues();
    return params;
}

SurfaceParams SurfaceParams::CreateForFramebuffer(Core::System& system, std::size_t index) {
    const auto& config{system.GPU().Maxwell3D().regs.rt[index]};
    SurfaceParams params;
    params.is_tiled =
        config.memory_layout.type == Tegra::Engines::Maxwell3D::Regs::InvMemoryLayout::BlockLinear;
    params.block_width = 1 << config.memory_layout.block_width;
    params.block_height = 1 << config.memory_layout.block_height;
    params.block_depth = 1 << config.memory_layout.block_depth;
    params.tile_width_spacing = 1;
    params.pixel_format = PixelFormatFromRenderTargetFormat(config.format);
    params.component_type = ComponentTypeFromRenderTarget(config.format);
    params.type = GetFormatType(params.pixel_format);
    if (params.is_tiled) {
        params.width = config.width;
    } else {
        const u32 bpp = GetFormatBpp(params.pixel_format) / CHAR_BIT;
        params.pitch = config.width;
        params.width = params.pitch / bpp;
    }
    params.height = config.height;
    params.depth = 1;
    params.unaligned_height = config.height;
    params.target = SurfaceTarget::Texture2D;
    params.num_levels = 1;

    params.CalculateCachedValues();
    return params;
}

SurfaceParams SurfaceParams::CreateForFermiCopySurface(
    const Tegra::Engines::Fermi2D::Regs::Surface& config) {
    SurfaceParams params{};
    params.is_tiled = !config.linear;
    params.block_width = params.is_tiled ? std::min(config.BlockWidth(), 32U) : 0,
    params.block_height = params.is_tiled ? std::min(config.BlockHeight(), 32U) : 0,
    params.block_depth = params.is_tiled ? std::min(config.BlockDepth(), 32U) : 0,
    params.tile_width_spacing = 1;
    params.pixel_format = PixelFormatFromRenderTargetFormat(config.format);
    params.component_type = ComponentTypeFromRenderTarget(config.format);
    params.type = GetFormatType(params.pixel_format);
    params.width = config.width;
    params.height = config.height;
    params.unaligned_height = config.height;
    // TODO(Rodrigo): Try to guess the surface target from depth and layer parameters
    params.target = SurfaceTarget::Texture2D;
    params.depth = 1;
    params.num_levels = 1;

    params.CalculateCachedValues();
    return params;
}

u32 SurfaceParams::GetMipWidth(u32 level) const {
    return std::max(1U, width >> level);
}

u32 SurfaceParams::GetMipHeight(u32 level) const {
    return std::max(1U, height >> level);
}

u32 SurfaceParams::GetMipDepth(u32 level) const {
    return IsLayered() ? depth : std::max(1U, depth >> level);
}

bool SurfaceParams::IsLayered() const {
    switch (target) {
    case SurfaceTarget::Texture1DArray:
    case SurfaceTarget::Texture2DArray:
    case SurfaceTarget::TextureCubeArray:
    case SurfaceTarget::TextureCubemap:
        return true;
    default:
        return false;
    }
}

u32 SurfaceParams::GetMipBlockHeight(u32 level) const {
    // Auto block resizing algorithm from:
    // https://cgit.freedesktop.org/mesa/mesa/tree/src/gallium/drivers/nouveau/nv50/nv50_miptree.c
    if (level == 0) {
        return block_height;
    }
    const u32 height{GetMipHeight(level)};
    const u32 default_block_height{GetDefaultBlockHeight(pixel_format)};
    const u32 blocks_in_y{(height + default_block_height - 1) / default_block_height};
    u32 block_height = 16;
    while (block_height > 1 && blocks_in_y <= block_height * 4) {
        block_height >>= 1;
    }
    return block_height;
}

u32 SurfaceParams::GetMipBlockDepth(u32 level) const {
    if (level == 0)
        return block_depth;
    if (target != SurfaceTarget::Texture3D)
        return 1;

    const u32 depth{GetMipDepth(level)};
    u32 block_depth = 32;
    while (block_depth > 1 && depth * 2 <= block_depth) {
        block_depth >>= 1;
    }
    if (block_depth == 32 && GetMipBlockHeight(level) >= 4) {
        return 16;
    }
    return block_depth;
}

std::size_t SurfaceParams::GetGuestMipmapLevelOffset(u32 level) const {
    std::size_t offset = 0;
    for (u32 i = 0; i < level; i++) {
        offset += GetInnerMipmapMemorySize(i, false, IsLayered(), false);
    }
    return offset;
}

std::size_t SurfaceParams::GetHostMipmapLevelOffset(u32 level) const {
    std::size_t offset = 0;
    for (u32 i = 0; i < level; i++) {
        offset += GetInnerMipmapMemorySize(i, true, false, false);
    }
    return offset;
}

std::size_t SurfaceParams::GetGuestLayerSize() const {
    return GetInnerMemorySize(false, true, false);
}

std::size_t SurfaceParams::GetHostLayerSize(u32 level) const {
    return GetInnerMipmapMemorySize(level, true, IsLayered(), false);
}

bool SurfaceParams::IsFamiliar(const SurfaceParams& view_params) const {
    if (std::tie(is_tiled, tile_width_spacing, pixel_format, component_type, type) !=
        std::tie(view_params.is_tiled, view_params.tile_width_spacing, view_params.pixel_format,
                 view_params.component_type, view_params.type)) {
        return false;
    }

    const SurfaceTarget view_target{view_params.target};
    if (view_target == target) {
        return true;
    }

    switch (target) {
    case SurfaceTarget::Texture1D:
    case SurfaceTarget::Texture2D:
    case SurfaceTarget::Texture3D:
        return false;
    case SurfaceTarget::Texture1DArray:
        return view_target == SurfaceTarget::Texture1D;
    case SurfaceTarget::Texture2DArray:
        return view_target == SurfaceTarget::Texture2D;
    case SurfaceTarget::TextureCubemap:
        return view_target == SurfaceTarget::Texture2D ||
               view_target == SurfaceTarget::Texture2DArray;
    case SurfaceTarget::TextureCubeArray:
        return view_target == SurfaceTarget::Texture2D ||
               view_target == SurfaceTarget::Texture2DArray ||
               view_target == SurfaceTarget::TextureCubemap;
    default:
        UNIMPLEMENTED_MSG("Unimplemented texture family={}", static_cast<u32>(target));
        return false;
    }
}

bool SurfaceParams::IsPixelFormatZeta() const {
    return pixel_format >= VideoCore::Surface::PixelFormat::MaxColorFormat &&
           pixel_format < VideoCore::Surface::PixelFormat::MaxDepthStencilFormat;
}

void SurfaceParams::CalculateCachedValues() {
    guest_size_in_bytes = GetInnerMemorySize(false, false, false);

    // ASTC is uncompressed in software, in emulated as RGBA8
    if (IsPixelFormatASTC(pixel_format)) {
        host_size_in_bytes = width * height * depth * 4;
    } else {
        host_size_in_bytes = GetInnerMemorySize(true, false, false);
    }

    switch (target) {
    case SurfaceTarget::Texture1D:
    case SurfaceTarget::Texture2D:
    case SurfaceTarget::Texture3D:
        num_layers = 1;
        break;
    case SurfaceTarget::Texture1DArray:
    case SurfaceTarget::Texture2DArray:
    case SurfaceTarget::TextureCubemap:
    case SurfaceTarget::TextureCubeArray:
        num_layers = depth;
        break;
    default:
        UNREACHABLE();
    }
}

std::size_t SurfaceParams::GetInnerMipmapMemorySize(u32 level, bool as_host_size, bool layer_only,
                                                    bool uncompressed) const {
    const bool tiled{as_host_size ? false : is_tiled};
    const u32 tile_x{GetDefaultBlockWidth(pixel_format)};
    const u32 tile_y{GetDefaultBlockHeight(pixel_format)};
    const u32 width{GetMipmapSize(uncompressed, GetMipWidth(level), tile_x)};
    const u32 height{GetMipmapSize(uncompressed, GetMipHeight(level), tile_y)};
    const u32 depth{layer_only ? 1U : GetMipDepth(level)};
    return Tegra::Texture::CalculateSize(tiled, GetBytesPerPixel(pixel_format), width, height,
                                         depth, GetMipBlockHeight(level), GetMipBlockDepth(level));
}

std::size_t SurfaceParams::GetInnerMemorySize(bool as_host_size, bool layer_only,
                                              bool uncompressed) const {
    std::size_t size = 0;
    for (u32 level = 0; level < num_levels; ++level) {
        size += GetInnerMipmapMemorySize(level, as_host_size, layer_only, uncompressed);
    }
    if (!as_host_size && is_tiled) {
        size = Common::AlignUp(size, Tegra::Texture::GetGOBSize() * block_height * block_depth);
    }
    return size;
}

std::map<u64, std::pair<u32, u32>> SurfaceParams::CreateViewOffsetMap() const {
    std::map<u64, std::pair<u32, u32>> view_offset_map;
    switch (target) {
    case SurfaceTarget::Texture1D:
    case SurfaceTarget::Texture2D:
    case SurfaceTarget::Texture3D: {
        constexpr u32 layer = 0;
        for (u32 level = 0; level < num_levels; ++level) {
            const std::size_t offset{GetGuestMipmapLevelOffset(level)};
            view_offset_map.insert({offset, {layer, level}});
        }
        break;
    }
    case SurfaceTarget::Texture1DArray:
    case SurfaceTarget::Texture2DArray:
    case SurfaceTarget::TextureCubemap:
    case SurfaceTarget::TextureCubeArray: {
        const std::size_t layer_size{GetGuestLayerSize()};
        for (u32 level = 0; level < num_levels; ++level) {
            const std::size_t level_offset{GetGuestMipmapLevelOffset(level)};
            for (u32 layer = 0; layer < num_layers; ++layer) {
                const auto layer_offset{static_cast<std::size_t>(layer_size * layer)};
                const std::size_t offset{level_offset + layer_offset};
                view_offset_map.insert({offset, {layer, level}});
            }
        }
        break;
    }
    default:
        UNIMPLEMENTED_MSG("Unimplemented surface target {}", static_cast<u32>(target));
    }
    return view_offset_map;
}

bool SurfaceParams::IsViewValid(const SurfaceParams& view_params, u32 layer, u32 level) const {
    return IsDimensionValid(view_params, level) && IsDepthValid(view_params, level) &&
           IsInBounds(view_params, layer, level);
}

bool SurfaceParams::IsDimensionValid(const SurfaceParams& view_params, u32 level) const {
    return view_params.width == GetMipWidth(level) && view_params.height == GetMipHeight(level);
}

bool SurfaceParams::IsDepthValid(const SurfaceParams& view_params, u32 level) const {
    if (view_params.target != SurfaceTarget::Texture3D) {
        return true;
    }
    return view_params.depth == GetMipDepth(level);
}

bool SurfaceParams::IsInBounds(const SurfaceParams& view_params, u32 layer, u32 level) const {
    return layer + view_params.num_layers <= num_layers &&
           level + view_params.num_levels <= num_levels;
}

std::size_t HasheableSurfaceParams::Hash() const {
    return static_cast<std::size_t>(
        Common::CityHash64(reinterpret_cast<const char*>(this), sizeof(*this)));
}

bool HasheableSurfaceParams::operator==(const HasheableSurfaceParams& rhs) const {
    return std::tie(is_tiled, block_width, block_height, block_depth, tile_width_spacing, width,
                    height, depth, pitch, unaligned_height, num_levels, pixel_format,
                    component_type, type, target) ==
           std::tie(rhs.is_tiled, rhs.block_width, rhs.block_height, rhs.block_depth,
                    rhs.tile_width_spacing, rhs.width, rhs.height, rhs.depth, rhs.pitch,
                    rhs.unaligned_height, rhs.num_levels, rhs.pixel_format, rhs.component_type,
                    rhs.type, rhs.target);
}

std::size_t ViewKey::Hash() const {
    return static_cast<std::size_t>(
        Common::CityHash64(reinterpret_cast<const char*>(this), sizeof(*this)));
}

bool ViewKey::operator==(const ViewKey& rhs) const {
    return std::tie(base_layer, num_layers, base_level, num_levels) ==
           std::tie(rhs.base_layer, rhs.num_layers, rhs.base_level, rhs.num_levels);
}

} // namespace VideoCommon
