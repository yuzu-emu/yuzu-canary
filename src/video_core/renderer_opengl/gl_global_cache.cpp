// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "core/core.h"
#include "core/memory.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_opengl/gl_global_cache.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/renderer_opengl/gl_shader_cache.h"
#include "video_core/renderer_opengl/gl_shader_manager.h"
#include "video_core/renderer_opengl/utils.h"

namespace OpenGL {

CachedGlobalRegion::CachedGlobalRegion(VAddr addr, u32 size) : addr{addr}, size{size} {
    buffer.Create();
    LabelGLObject(GL_BUFFER, buffer.handle, addr);
}

/// Helper function to get the maximum size we can use for an OpenGL uniform block
static u32 GetMaxUniformBlockSize() {
    GLint max_size{};
    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &max_size);
    return static_cast<u32>(max_size);
}

void CachedGlobalRegion::Reload(u32 size_) {
    static const u32 max_size{GetMaxUniformBlockSize()};

    size = size_;
    if (size > max_size) {
        size = max_size;
        LOG_CRITICAL(HW_GPU, "Global region size {} exceeded max UBO size of {}!", size_, max_size);
    }

    glBindBuffer(GL_UNIFORM_BUFFER, buffer.handle);
    glBufferData(GL_UNIFORM_BUFFER, size, Memory::GetPointer(addr), GL_DYNAMIC_DRAW);
}

GlobalRegion GlobalRegionCacheOpenGL::TryGetReservedGlobalRegion(VAddr addr, u32 size) const {
    auto search{reserve.find(addr)};
    if (search == reserve.end()) {
        return {};
    }
    return search->second;
}

GlobalRegion GlobalRegionCacheOpenGL::GetUncachedGlobalRegion(VAddr addr, u32 size) {
    GlobalRegion region{TryGetReservedGlobalRegion(addr, size)};
    if (!region) {
        // No reserved surface available, create a new one and reserve it
        region = std::make_shared<CachedGlobalRegion>(addr, size);
        ReserveGlobalRegion(region);
    }
    region->Reload(size);
    return region;
}

void GlobalRegionCacheOpenGL::ReserveGlobalRegion(const GlobalRegion& region) {
    reserve[region->GetAddr()] = region;
}

GlobalRegionCacheOpenGL::GlobalRegionCacheOpenGL(RasterizerOpenGL& rasterizer)
    : RasterizerCache{rasterizer} {}

GlobalRegion GlobalRegionCacheOpenGL::GetGlobalRegion(
    const Tegra::Engines::Maxwell3D::GlobalMemoryDescriptor& global_region,
    Tegra::Engines::Maxwell3D::Regs::ShaderStage stage) {
    auto& gpu{Core::System::GetInstance().GPU()};
    const auto cbufs = gpu.Maxwell3D().state.shader_stages[static_cast<u64>(stage)];
    const auto cbuf_addr{gpu.MemoryManager().GpuToCpuAddress(
        cbufs.const_buffers[global_region.cbuf_index].address + global_region.cbuf_offset)};

    ASSERT(cbuf_addr);

    const auto actual_addr_gpu = Memory::Read64(*cbuf_addr);
    const auto size = Memory::Read32(*cbuf_addr + 8);
    const auto actual_addr{gpu.MemoryManager().GpuToCpuAddress(actual_addr_gpu)};

    ASSERT(actual_addr);

    // Look up global region in the cache based on address
    GlobalRegion region{TryGet(*actual_addr)};

    if (!region) {
        // No global region found - create a new one
        region = GetUncachedGlobalRegion(*actual_addr, size);
        Register(region);
    }

    return region;
}

} // namespace OpenGL
