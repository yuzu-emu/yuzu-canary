// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <glad/glad.h>

#include "common/assert.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/memory.h"
#include "video_core/renderer_opengl/gl_global_cache.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/renderer_opengl/gl_shader_decompiler.h"
#include "video_core/renderer_opengl/utils.h"

namespace OpenGL {

CachedGlobalRegion::CachedGlobalRegion(VAddr addr, u32 size) : addr{addr}, size{size} {
    buffer.Create();
    // Bind and unbind the buffer so it gets allocated by the driver
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer.handle);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    LabelGLObject(GL_BUFFER, buffer.handle, addr, "GlobalMemory");
}

void CachedGlobalRegion::Reload(u32 size_) {
    constexpr auto max_size = static_cast<u32>(RasterizerOpenGL::MaxGlobalMemorySize);

    size = size_;
    if (size > max_size) {
        size = max_size;
        LOG_CRITICAL(HW_GPU, "Global region size {} exceeded the expected size {}!", size_,
                     max_size);
    }

    // TODO(Rodrigo): Get rid of Memory::GetPointer with a staging buffer
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer.handle);
    glBufferData(GL_SHADER_STORAGE_BUFFER, size, Memory::GetPointer(addr), GL_DYNAMIC_DRAW);
}

GlobalRegion GlobalRegionCacheOpenGL::TryGetReservedGlobalRegion(VAddr addr, u32 size) const {
    const auto search{reserve.find(addr)};
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
    const GLShader::GlobalMemoryEntry& global_region,
    Tegra::Engines::Maxwell3D::Regs::ShaderStage stage) {

    auto& gpu{Core::System::GetInstance().GPU()};
    const auto cbufs = gpu.Maxwell3D().state.shader_stages[static_cast<u64>(stage)];
    const auto cbuf_addr = gpu.MemoryManager().GpuToCpuAddress(
        cbufs.const_buffers[global_region.GetCbufIndex()].address + global_region.GetCbufOffset());
    ASSERT(cbuf_addr);

    const auto actual_addr_gpu = Memory::Read64(*cbuf_addr);
    const auto size = Memory::Read32(*cbuf_addr + 8);
    const auto actual_addr = gpu.MemoryManager().GpuToCpuAddress(actual_addr_gpu);
    ASSERT(actual_addr);

    // Look up global region in the cache based on address
    GlobalRegion region = TryGet(*actual_addr);

    if (!region) {
        // No global region found - create a new one
        region = GetUncachedGlobalRegion(*actual_addr, size);
        Register(region);
    }

    return region;
}

} // namespace OpenGL
