// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <fmt/format.h>
#include "common/common_types.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/rasterizer_cache.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"

namespace OpenGL {

class RasterizerOpenGL;
class CachedGlobalRegion;
using GlobalRegion = std::shared_ptr<CachedGlobalRegion>;

/// Helper class for caching global region uniform locations
class CachedGlobalRegionUniform {
public:
    explicit CachedGlobalRegionUniform(std::size_t index) : index{index} {}

    std::string GetName() const {
        return fmt::format("global_memory_region_declblock_{}", index);
    }

    u32 GetHash() const {
        // This needs to be unique from ConstBufferEntry::GetHash and SamplerEntry::GetHash
        return (static_cast<u32>(index) << 16) | 0xFFFF;
    }

private:
    std::size_t index{};
};

class CachedGlobalRegion final : public RasterizerCacheObject {
public:
    CachedGlobalRegion(VAddr addr, u32 size);

    /// Gets the address of the shader in guest memory, required for cache management
    VAddr GetAddr() const {
        return addr;
    }

    /// Gets the size of the shader in guest memory, required for cache management
    std::size_t GetSizeInBytes() const {
        return size;
    }

    /// Gets the GL program handle for the buffer
    GLuint GetBufferHandle() const {
        return buffer.handle;
    }

    /// Reloads the global region from guest memory
    void Reload(u32 size_);

    // We do not have to flush this cache as things in it are never modified by us.
    void Flush() override {}

private:
    VAddr addr;
    u32 size;

    OGLBuffer buffer;
};

class GlobalRegionCacheOpenGL final : public RasterizerCache<GlobalRegion> {
public:
    explicit GlobalRegionCacheOpenGL(RasterizerOpenGL& rasterizer);

    /// Gets the current specified shader stage program
    GlobalRegion GetGlobalRegion(
        const Tegra::Engines::Maxwell3D::GlobalMemoryDescriptor& descriptor,
        Tegra::Engines::Maxwell3D::Regs::ShaderStage stage);

private:
    GlobalRegion TryGetReservedGlobalRegion(VAddr addr, u32 size) const;
    GlobalRegion GetUncachedGlobalRegion(VAddr addr, u32 size);
    void ReserveGlobalRegion(const GlobalRegion& region);

    std::unordered_map<VAddr, GlobalRegion> reserve;
};

} // namespace OpenGL
