// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <utility>

#include <boost/icl/interval_map.hpp>
#include <glad/glad.h>

#include "common/common_types.h"
#include "video_core/engines/const_buffer_info.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/rasterizer_cache.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_opengl/gl_buffer_cache.h"
#include "video_core/renderer_opengl/gl_device.h"
#include "video_core/renderer_opengl/gl_framebuffer_cache.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_sampler_cache.h"
#include "video_core/renderer_opengl/gl_shader_cache.h"
#include "video_core/renderer_opengl/gl_shader_decompiler.h"
#include "video_core/renderer_opengl/gl_shader_manager.h"
#include "video_core/renderer_opengl/gl_state.h"
#include "video_core/renderer_opengl/gl_texture_cache.h"
#include "video_core/renderer_opengl/utils.h"
#include "video_core/textures/texture.h"

namespace Core {
class System;
}

namespace Core::Frontend {
class EmuWindow;
}

namespace Tegra {
class MemoryManager;
}

namespace OpenGL {

struct ScreenInfo;
struct DrawParameters;

class RasterizerOpenGL : public VideoCore::RasterizerInterface {
public:
    explicit RasterizerOpenGL(Core::System& system, Core::Frontend::EmuWindow& emu_window,
                              ScreenInfo& info);
    ~RasterizerOpenGL() override;

    bool DrawBatch(bool is_indexed) override;
    bool DrawMultiBatch(bool is_indexed) override;
    void Clear() override;
    void DispatchCompute(GPUVAddr code_addr) override;
    void FlushAll() override;
    void FlushRegion(CacheAddr addr, u64 size) override;
    void InvalidateRegion(CacheAddr addr, u64 size) override;
    void FlushAndInvalidateRegion(CacheAddr addr, u64 size) override;
    void FlushCommands() override;
    void TickFrame() override;
    bool AccelerateSurfaceCopy(const Tegra::Engines::Fermi2D::Regs::Surface& src,
                               const Tegra::Engines::Fermi2D::Regs::Surface& dst,
                               const Tegra::Engines::Fermi2D::Config& copy_config) override;
    bool AccelerateDisplay(const Tegra::FramebufferConfig& config, VAddr framebuffer_addr,
                           u32 pixel_stride) override;
    void UpdatePagesCachedCount(VAddr addr, u64 size, int delta) override;
    void LoadDiskResources(const std::atomic_bool& stop_loading,
                           const VideoCore::DiskResourceLoadCallback& callback) override;

private:
    /// Configures the color and depth framebuffer states.
    void ConfigureFramebuffers();

    void ConfigureClearFramebuffer(OpenGLState& current_state, bool using_color_fb,
                                   bool using_depth_fb, bool using_stencil_fb);

    /// Configures the current constbuffers to use for the draw command.
    void SetupDrawConstBuffers(Tegra::Engines::Maxwell3D::Regs::ShaderStage stage,
                               const Shader& shader);

    /// Configures the current constbuffers to use for the kernel invocation.
    void SetupComputeConstBuffers(const Shader& kernel);

    /// Configures a constant buffer.
    void SetupConstBuffer(const Tegra::Engines::ConstBufferInfo& buffer,
                          const GLShader::ConstBufferEntry& entry);

    /// Configures the current global memory entries to use for the draw command.
    void SetupDrawGlobalMemory(Tegra::Engines::Maxwell3D::Regs::ShaderStage stage,
                               const Shader& shader);

    /// Configures the current global memory entries to use for the kernel invocation.
    void SetupComputeGlobalMemory(const Shader& kernel);

    /// Configures a constant buffer.
    void SetupGlobalMemory(const GLShader::GlobalMemoryEntry& entry, GPUVAddr gpu_addr,
                           std::size_t size);

    /// Syncs all the state, shaders, render targets and textures setting before a draw call.
    void DrawPrelude();

    /// Configures the current textures to use for the draw command. Returns shaders texture buffer
    /// usage.
    TextureBufferUsage SetupDrawTextures(Tegra::Engines::Maxwell3D::Regs::ShaderStage stage,
                                         const Shader& shader, BaseBindings base_bindings);

    /// Configures the textures used in a compute shader. Returns texture buffer usage.
    TextureBufferUsage SetupComputeTextures(const Shader& kernel);

    /// Configures a texture. Returns true when the texture is a texture buffer.
    bool SetupTexture(u32 binding, const Tegra::Texture::FullTextureInfo& texture,
                      const GLShader::SamplerEntry& entry);

    /// Configures images in a compute shader.
    void SetupComputeImages(const Shader& shader);

    /// Configures an image.
    void SetupImage(u32 binding, const Tegra::Texture::TICEntry& tic,
                    const GLShader::ImageEntry& entry);

    /// Syncs the viewport and depth range to match the guest state
    void SyncViewport(OpenGLState& current_state, bool rescaling);

    /// Syncs the clip enabled status to match the guest state
    void SyncClipEnabled(
        const std::array<bool, Tegra::Engines::Maxwell3D::Regs::NumClipDistances>& clip_mask);

    /// Syncs the clip coefficients to match the guest state
    void SyncClipCoef();

    /// Syncs the cull mode to match the guest state
    void SyncCullMode();

    /// Syncs the primitve restart to match the guest state
    void SyncPrimitiveRestart();

    /// Syncs the depth test state to match the guest state
    void SyncDepthTestState();

    /// Syncs the stencil test state to match the guest state
    void SyncStencilTestState();

    /// Syncs the blend state to match the guest state
    void SyncBlendState();

    /// Syncs the LogicOp state to match the guest state
    void SyncLogicOpState();

    /// Syncs the the color clamp state
    void SyncFragmentColorClampState();

    /// Syncs the alpha coverage and alpha to one
    void SyncMultiSampleState();

    /// Syncs the scissor test state to match the guest state
    void SyncScissorTest(OpenGLState& current_state, bool rescaling);

    /// Syncs the transform feedback state to match the guest state
    void SyncTransformFeedback();

    /// Syncs the point state to match the guest state
    void SyncPointState();

    /// Syncs Color Mask
    void SyncColorMask();

    /// Syncs the polygon offsets
    void SyncPolygonOffset();

    /// Syncs the alpha test state to match the guest state
    void SyncAlphaTest();

    /// Check for extension that are not strictly required
    /// but are needed for correct emulation
    void CheckExtensions();

    const Device device;
    OpenGLState state;

    TextureCacheOpenGL texture_cache;
    ShaderCacheOpenGL shader_cache;
    SamplerCacheOpenGL sampler_cache;
    FramebufferCacheOpenGL framebuffer_cache;

    Core::System& system;
    ScreenInfo& screen_info;

    std::unique_ptr<GLShader::ProgramManager> shader_program_manager;
    std::map<std::array<Tegra::Engines::Maxwell3D::Regs::VertexAttribute,
                        Tegra::Engines::Maxwell3D::Regs::NumVertexAttributes>,
             OGLVertexArray>
        vertex_array_cache;

    static constexpr std::size_t STREAM_BUFFER_SIZE = 128 * 1024 * 1024;
    OGLBufferCache buffer_cache;

    VertexArrayPushBuffer vertex_array_pushbuffer;
    BindBuffersRangePushBuffer bind_ubo_pushbuffer{GL_UNIFORM_BUFFER};
    BindBuffersRangePushBuffer bind_ssbo_pushbuffer{GL_SHADER_STORAGE_BUFFER};

    std::size_t CalculateVertexArraysSize() const;

    std::size_t CalculateIndexBufferSize() const;

    /// Updates and returns a vertex array object representing current vertex format
    GLuint SetupVertexFormat();

    void SetupVertexBuffer(GLuint vao);
    void SetupVertexInstances(GLuint vao);

    GLintptr SetupIndexBuffer();

    GLintptr index_buffer_offset;

    void SetupShaders(GLenum primitive_mode);

    enum class AccelDraw { Disabled, Arrays, Indexed };
    AccelDraw accelerate_draw = AccelDraw::Disabled;

    OGLFramebuffer clear_framebuffer;

    using CachedPageMap = boost::icl::interval_map<u64, int>;
    CachedPageMap cached_pages;

    std::mutex pages_mutex;
};

} // namespace OpenGL
