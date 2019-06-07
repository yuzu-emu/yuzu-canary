// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>

#include <boost/icl/interval_map.hpp>
#include <glad/glad.h>

#include "common/common_types.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/rasterizer_cache.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_opengl/gl_buffer_cache.h"
#include "video_core/renderer_opengl/gl_device.h"
#include "video_core/renderer_opengl/gl_global_cache.h"
#include "video_core/renderer_opengl/gl_rasterizer_cache.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_sampler_cache.h"
#include "video_core/renderer_opengl/gl_shader_cache.h"
#include "video_core/renderer_opengl/gl_shader_manager.h"
#include "video_core/renderer_opengl/gl_state.h"
#include "video_core/renderer_opengl/utils.h"

namespace Core {
class System;
}

namespace Core::Frontend {
class EmuWindow;
}

namespace OpenGL {

struct ScreenInfo;
struct DrawParameters;
struct FramebufferCacheKey;

class RasterizerOpenGL : public VideoCore::RasterizerInterface {
public:
    explicit RasterizerOpenGL(Core::System& system, Core::Frontend::EmuWindow& emu_window,
                              ScreenInfo& info);
    ~RasterizerOpenGL() override;

    void DrawArrays() override;
    void Clear() override;
    void FlushAll() override;
    void FlushRegion(CacheAddr addr, u64 size) override;
    void InvalidateRegion(CacheAddr addr, u64 size) override;
    void FlushAndInvalidateRegion(CacheAddr addr, u64 size) override;
    bool AccelerateSurfaceCopy(const Tegra::Engines::Fermi2D::Regs::Surface& src,
                               const Tegra::Engines::Fermi2D::Regs::Surface& dst,
                               const Common::Rectangle<u32>& src_rect,
                               const Common::Rectangle<u32>& dst_rect) override;
    bool AccelerateDisplay(const Tegra::FramebufferConfig& config, VAddr framebuffer_addr,
                           u32 pixel_stride) override;
    bool AccelerateDrawBatch(bool is_indexed) override;
    void UpdatePagesCachedCount(VAddr addr, u64 size, int delta) override;
    void LoadDiskResources(const std::atomic_bool& stop_loading,
                           const VideoCore::DiskResourceLoadCallback& callback) override;

    /// Maximum supported size that a constbuffer can have in bytes.
    static constexpr std::size_t MaxConstbufferSize = 0x10000;
    static_assert(MaxConstbufferSize % sizeof(GLvec4) == 0,
                  "The maximum size of a constbuffer must be a multiple of the size of GLvec4");

private:
    struct FramebufferConfigState {
        bool using_color_fb{};
        bool using_depth_fb{};
        bool preserve_contents{};
        std::optional<std::size_t> single_color_target;

        bool operator==(const FramebufferConfigState& rhs) const {
            return std::tie(using_color_fb, using_depth_fb, preserve_contents,
                            single_color_target) == std::tie(rhs.using_color_fb, rhs.using_depth_fb,
                                                             rhs.preserve_contents,
                                                             rhs.single_color_target);
        }
        bool operator!=(const FramebufferConfigState& rhs) const {
            return !operator==(rhs);
        }
    };

    /**
     * Configures the color and depth framebuffer states.
     * @param use_color_fb If true, configure color framebuffers.
     * @param using_depth_fb If true, configure the depth/stencil framebuffer.
     * @param preserve_contents If true, tries to preserve data from a previously used framebuffer.
     * @param single_color_target Specifies if a single color buffer target should be used.
     * @returns If depth (first) or stencil (second) are being stored in the bound zeta texture
     * (requires using_depth_fb to be true)
     */
    std::pair<bool, bool> ConfigureFramebuffers(
        OpenGLState& current_state, bool use_color_fb = true, bool using_depth_fb = true,
        bool preserve_contents = true, std::optional<std::size_t> single_color_target = {});

    /// Configures the current constbuffers to use for the draw command.
    void SetupConstBuffers(Tegra::Engines::Maxwell3D::Regs::ShaderStage stage, const Shader& shader,
                           GLuint program_handle, BaseBindings base_bindings);

    /// Configures the current global memory entries to use for the draw command.
    void SetupGlobalRegions(Tegra::Engines::Maxwell3D::Regs::ShaderStage stage,
                            const Shader& shader, GLenum primitive_mode,
                            BaseBindings base_bindings);

    /// Configures the current textures to use for the draw command.
    void SetupTextures(Tegra::Engines::Maxwell3D::Regs::ShaderStage stage, const Shader& shader,
                       GLuint program_handle, BaseBindings base_bindings);

    /// Syncs the viewport and depth range to match the guest state
    void SyncViewport(OpenGLState& current_state);

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
    void SyncScissorTest(OpenGLState& current_state);

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

    RasterizerCacheOpenGL res_cache;
    ShaderCacheOpenGL shader_cache;
    GlobalRegionCacheOpenGL global_cache;
    SamplerCacheOpenGL sampler_cache;

    Core::System& system;
    ScreenInfo& screen_info;

    std::unique_ptr<GLShader::ProgramManager> shader_program_manager;
    std::map<std::array<Tegra::Engines::Maxwell3D::Regs::VertexAttribute,
                        Tegra::Engines::Maxwell3D::Regs::NumVertexAttributes>,
             OGLVertexArray>
        vertex_array_cache;

    std::map<FramebufferCacheKey, OGLFramebuffer> framebuffer_cache;
    FramebufferConfigState current_framebuffer_config_state;
    std::pair<bool, bool> current_depth_stencil_usage{};

    static constexpr std::size_t STREAM_BUFFER_SIZE = 128 * 1024 * 1024;
    OGLBufferCache buffer_cache;

    BindBuffersRangePushBuffer bind_ubo_pushbuffer{GL_UNIFORM_BUFFER};
    BindBuffersRangePushBuffer bind_ssbo_pushbuffer{GL_SHADER_STORAGE_BUFFER};

    std::size_t CalculateVertexArraysSize() const;

    std::size_t CalculateIndexBufferSize() const;

    /// Updates and returns a vertex array object representing current vertex format
    GLuint SetupVertexFormat();

    void SetupVertexBuffer(GLuint vao);

    DrawParameters SetupDraw();

    void SetupShaders(GLenum primitive_mode);

    void SetupCachedFramebuffer(const FramebufferCacheKey& fbkey, OpenGLState& current_state);

    enum class AccelDraw { Disabled, Arrays, Indexed };
    AccelDraw accelerate_draw = AccelDraw::Disabled;

    using CachedPageMap = boost::icl::interval_map<u64, int>;
    CachedPageMap cached_pages;
};

} // namespace OpenGL
