// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "core/core.h"
#include "core/memory.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_opengl/gl_global_cache.h"
#include "video_core/renderer_opengl/gl_shader_cache.h"
#include "video_core/renderer_opengl/gl_shader_manager.h"
#include "video_core/utils.h"

namespace OpenGL {

CachedGlobalRegion::CachedGlobalRegion(VAddr addr, u32 size) : addr{addr}, size{size} {
    buffer.Create();
    VideoCore::LabelGLObject(GL_BUFFER, buffer.handle, addr);
}

void CachedGlobalRegion::Reload(u32 size_) {
    size = size_;
    glBindBuffer(GL_UNIFORM_BUFFER, buffer.handle);
    glBufferData(GL_UNIFORM_BUFFER, size, Memory::GetPointer(addr), GL_DYNAMIC_DRAW);
}

GlobalRegion GlobalRegionCacheOpenGL::TryGetReservedGlobalRegion(VAddr addr, u32 size) {
    auto search{reserve.find(addr)};
    if (search != reserve.end()) {
        return search->second;
    }
    return {};
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

GlobalRegion GlobalRegionCacheOpenGL::GetGlobalRegion(
    const Tegra::Engines::Maxwell3D::GlobalMemoryDescriptor& global_region,
    Tegra::Engines::Maxwell3D::Regs::ShaderStage stage) {
    auto& gpu{Core::System::GetInstance().GPU()};
    const auto cbufs = gpu.Maxwell3D().state.shader_stages[static_cast<u64>(stage)];
    const auto cbuf_addr{gpu.MemoryManager().GpuToCpuAddress(
        cbufs.const_buffers[global_region.cbuf_index].address + global_region.cbuf_offset)};

    ASSERT(cbuf_addr != boost::none);

    const auto actual_addr_gpu = Memory::Read64(cbuf_addr.get());
    const auto size = Memory::Read32(cbuf_addr.get() + 8);
    const auto actual_addr{gpu.MemoryManager().GpuToCpuAddress(actual_addr_gpu)};

    ASSERT(actual_addr != boost::none);

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
