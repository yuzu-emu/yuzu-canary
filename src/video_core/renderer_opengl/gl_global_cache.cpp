// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <glad/glad.h>

#include "common/assert.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "video_core/renderer_opengl/gl_global_cache.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/renderer_opengl/gl_shader_decompiler.h"
#include "video_core/renderer_opengl/utils.h"

namespace OpenGL {

CachedGlobalRegion::CachedGlobalRegion(VAddr cpu_addr, u32 size, u8* host_ptr)
    : cpu_addr{cpu_addr}, size{size}, RasterizerCacheObject{host_ptr} {
    buffer.Create();
    // Bind and unbind the buffer so it gets allocated by the driver
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer.handle);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    LabelGLObject(GL_BUFFER, buffer.handle, cpu_addr, "GlobalMemory");
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
    glBufferData(GL_SHADER_STORAGE_BUFFER, size, GetHostPtr(), GL_DYNAMIC_DRAW);
}

GlobalRegion GlobalRegionCacheOpenGL::TryGetReservedGlobalRegion(CacheAddr addr, u32 size) const {
    const auto search{reserve.find(addr)};
    if (search == reserve.end()) {
        return {};
    }
    return search->second;
}

GlobalRegion GlobalRegionCacheOpenGL::GetUncachedGlobalRegion(GPUVAddr addr, u32 size,
                                                              u8* host_ptr) {
    GlobalRegion region{TryGetReservedGlobalRegion(ToCacheAddr(host_ptr), size)};
    if (!region) {
        // No reserved surface available, create a new one and reserve it
        auto& memory_manager{Core::System::GetInstance().GPU().MemoryManager()};
        const auto cpu_addr = *memory_manager.GpuToCpuAddress(addr);
        region = std::make_shared<CachedGlobalRegion>(cpu_addr, size, host_ptr);
        ReserveGlobalRegion(region);
    }
    region->Reload(size);
    return region;
}

void GlobalRegionCacheOpenGL::ReserveGlobalRegion(GlobalRegion region) {
    reserve.insert_or_assign(region->GetCacheAddr(), std::move(region));
}

GlobalRegionCacheOpenGL::GlobalRegionCacheOpenGL(RasterizerOpenGL& rasterizer)
    : RasterizerCache{rasterizer} {}

GlobalRegion GlobalRegionCacheOpenGL::GetGlobalRegion(
    const GLShader::GlobalMemoryEntry& global_region,
    Tegra::Engines::Maxwell3D::Regs::ShaderStage stage) {

    auto& gpu{Core::System::GetInstance().GPU()};
    auto& memory_manager{gpu.MemoryManager()};
    const auto cbufs{gpu.Maxwell3D().state.shader_stages[static_cast<u64>(stage)]};
    const auto addr{cbufs.const_buffers[global_region.GetCbufIndex()].address +
                    global_region.GetCbufOffset()};
    const auto actual_addr{memory_manager.Read<u64>(addr)};
    const auto size{memory_manager.Read<u32>(addr + 8)};

    // Look up global region in the cache based on address
    const auto& host_ptr{memory_manager.GetPointer(actual_addr)};
    GlobalRegion region{TryGet(host_ptr)};

    if (!region) {
        // No global region found - create a new one
        region = GetUncachedGlobalRegion(actual_addr, size, host_ptr);
        Register(region);
    }

    return region;
}

} // namespace OpenGL
