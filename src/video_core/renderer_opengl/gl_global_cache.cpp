// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <glad/glad.h>

#include "common/logging/log.h"
#include "core/core.h"
#include "video_core/memory_manager.h"
#include "video_core/renderer_opengl/gl_global_cache.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/renderer_opengl/gl_shader_decompiler.h"
#include "video_core/renderer_opengl/utils.h"

namespace OpenGL {

CachedGlobalRegion::CachedGlobalRegion(VAddr cpu_addr, u8* host_ptr, u32 size, u32 max_size)
    : RasterizerCacheObject{host_ptr}, cpu_addr{cpu_addr}, host_ptr{host_ptr}, size{size},
      max_size{max_size} {
    buffer.Create();
    LabelGLObject(GL_BUFFER, buffer.handle, cpu_addr, "GlobalMemory");
}

CachedGlobalRegion::~CachedGlobalRegion() = default;

void CachedGlobalRegion::Reload(u32 size_) {
    size = size_;
    if (size > max_size) {
        size = max_size;
        LOG_CRITICAL(HW_GPU, "Global region size {} exceeded the supported size {}!", size_,
                     max_size);
    }
    glNamedBufferData(buffer.handle, size, host_ptr, GL_STREAM_DRAW);
}

void CachedGlobalRegion::Flush() {
    LOG_DEBUG(Render_OpenGL, "Flushing {} bytes to CPU memory address 0x{:16}", size, cpu_addr);
    glGetNamedBufferSubData(buffer.handle, 0, static_cast<GLsizeiptr>(size), host_ptr);
}

GlobalRegion GlobalRegionCacheOpenGL::TryGetReservedGlobalRegion(CacheAddr addr, u32 size) const {
    const auto search{reserve.find(addr)};
    if (search == reserve.end()) {
        return {};
    }
    return search->second;
}

GlobalRegion GlobalRegionCacheOpenGL::GetUncachedGlobalRegion(GPUVAddr addr, u8* host_ptr,
                                                              u32 size) {
    GlobalRegion region{TryGetReservedGlobalRegion(ToCacheAddr(host_ptr), size)};
    if (!region) {
        // No reserved surface available, create a new one and reserve it
        auto& memory_manager{Core::System::GetInstance().GPU().MemoryManager()};
        const auto cpu_addr{memory_manager.GpuToCpuAddress(addr)};
        ASSERT(cpu_addr);

        region = std::make_shared<CachedGlobalRegion>(*cpu_addr, host_ptr, size, max_ssbo_size);
        ReserveGlobalRegion(region);
    }
    region->Reload(size);
    return region;
}

void GlobalRegionCacheOpenGL::ReserveGlobalRegion(GlobalRegion region) {
    reserve.insert_or_assign(region->GetCacheAddr(), std::move(region));
}

GlobalRegionCacheOpenGL::GlobalRegionCacheOpenGL(RasterizerOpenGL& rasterizer)
    : RasterizerCache{rasterizer} {
    GLint max_ssbo_size_;
    glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &max_ssbo_size_);
    max_ssbo_size = static_cast<u32>(max_ssbo_size_);
}

GlobalRegion GlobalRegionCacheOpenGL::GetGlobalRegion(
    const GLShader::GlobalMemoryEntry& global_region,
    Tegra::Engines::Maxwell3D::Regs::ShaderStage stage) {
    std::lock_guard lock{mutex};

    auto& gpu{Core::System::GetInstance().GPU()};
    auto& memory_manager{gpu.MemoryManager()};
    const auto cbufs{gpu.Maxwell3D().state.shader_stages[static_cast<std::size_t>(stage)]};
    const auto addr{cbufs.const_buffers[global_region.GetCbufIndex()].address +
                    global_region.GetCbufOffset()};
    const auto actual_addr{memory_manager.Read<u64>(addr)};
    const auto size{memory_manager.Read<u32>(addr + 8)};

    // Look up global region in the cache based on address
    const auto& host_ptr{memory_manager.GetPointer(actual_addr)};
    GlobalRegion region{TryGet(host_ptr)};

    if (!region) {
        // No global region found - create a new one
        region = GetUncachedGlobalRegion(actual_addr, host_ptr, size);
        Register(region);
    }

    return region;
}

} // namespace OpenGL
