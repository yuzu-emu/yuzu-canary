// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include <boost/icl/interval_map.hpp>
#include <boost/range/iterator_range.hpp>
#include <glad/glad.h>

#include "common/common_types.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_opengl/gl_rasterizer_cache.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_shader_cache.h"
#include "video_core/renderer_opengl/gl_shader_gen.h"
#include "video_core/renderer_opengl/gl_shader_manager.h"
#include "video_core/renderer_opengl/gl_state.h"
#include "video_core/renderer_opengl/gl_stream_buffer.h"

namespace Core::Frontend {
class EmuWindow;
}

namespace OpenGL {

struct ScreenInfo;

class RasterizerOpenGL : public VideoCore::RasterizerInterface {
public:
    explicit RasterizerOpenGL(Core::Frontend::EmuWindow& renderer, ScreenInfo& info);
    ~RasterizerOpenGL() override;

    void DrawArrays() override;
    void Clear() override;
    void NotifyMaxwellRegisterChanged(u32 method) override;
    void FlushAll() override;
    void FlushRegion(VAddr addr, u64 size) override;
    void InvalidateRegion(VAddr addr, u64 size) override;
    void FlushAndInvalidateRegion(VAddr addr, u64 size) override;
    bool AccelerateDisplayTransfer(const void* config) override;
    bool AccelerateTextureCopy(const void* config) override;
    bool AccelerateFill(const void* config) override;
    bool AccelerateDisplay(const Tegra::FramebufferConfig& config, VAddr framebuffer_addr,
                           u32 pixel_stride) override;
    bool AccelerateDrawBatch(bool is_indexed) override;
    void UpdatePagesCachedCount(Tegra::GPUVAddr addr, u64 size, int delta) override;

    /// OpenGL shader generated for a given Maxwell register state
    struct MaxwellShader {
        /// OpenGL shader resource
        OGLProgram shader;
    };

    struct VertexShader {
        OGLShader shader;
    };

    struct FragmentShader {
        OGLShader shader;
    };

    /// Maximum supported size that a constbuffer can have in bytes.
    static constexpr size_t MaxConstbufferSize = 0x10000;
    static_assert(MaxConstbufferSize % sizeof(GLvec4) == 0,
                  "The maximum size of a constbuffer must be a multiple of the size of GLvec4");

private:
    class SamplerInfo {
    public:
        OGLSampler sampler;

        /// Creates the sampler object, initializing its state so that it's in sync with the
        /// SamplerInfo struct.
        void Create();
        /// Syncs the sampler object with the config, updating any necessary state.
        void SyncWithConfig(const Tegra::Texture::TSCEntry& config);

    private:
        Tegra::Texture::TextureFilter mag_filter;
        Tegra::Texture::TextureFilter min_filter;
        Tegra::Texture::WrapMode wrap_u;
        Tegra::Texture::WrapMode wrap_v;
        GLvec4 border_color;
    };

    /// Configures the color and depth framebuffer states and returns the dirty <Color, Depth>
    /// surfaces if writing was enabled.
    std::pair<Surface, Surface> ConfigureFramebuffers(bool using_color_fb, bool using_depth_fb,
                                                      bool preserve_contents);

    /// Binds the framebuffer color and depth surface
    void BindFramebufferSurfaces(const Surface& color_surface, const Surface& depth_surface,
                                 bool has_stencil);

    /*
     * Configures the current constbuffers to use for the draw command.
     * @param stage The shader stage to configure buffers for.
     * @param shader The shader object that contains the specified stage.
     * @param current_bindpoint The offset at which to start counting new buffer bindpoints.
     * @returns The next available bindpoint for use in the next shader stage.
     */
    std::tuple<u8*, GLintptr, u32> SetupConstBuffers(
        u8* buffer_ptr, GLintptr buffer_offset, Tegra::Engines::Maxwell3D::Regs::ShaderStage stage,
        Shader& shader, u32 current_bindpoint);

    /*
     * Configures the current textures to use for the draw command.
     * @param stage The shader stage to configure textures for.
     * @param shader The shader object that contains the specified stage.
     * @param current_unit The offset at which to start counting unused texture units.
     * @returns The next available bindpoint for use in the next shader stage.
     */
    u32 SetupTextures(Tegra::Engines::Maxwell3D::Regs::ShaderStage stage, Shader& shader,
                      u32 current_unit);

    /// Syncs the viewport to match the guest state
    void SyncViewport(const MathUtil::Rectangle<u32>& surfaces_rect);

    /// Syncs the clip enabled status to match the guest state
    void SyncClipEnabled();

    /// Syncs the clip coefficients to match the guest state
    void SyncClipCoef();

    /// Syncs the cull mode to match the guest state
    void SyncCullMode();

    /// Syncs the depth scale to match the guest state
    void SyncDepthScale();

    /// Syncs the depth offset to match the guest state
    void SyncDepthOffset();

    /// Syncs the depth test state to match the guest state
    void SyncDepthTestState();

    /// Syncs the stencil test state to match the guest state
    void SyncStencilTestState();

    /// Syncs the blend state to match the guest state
    void SyncBlendState();

    /// Syncs the LogicOp state to match the guest state
    void SyncLogicOpState();

    bool has_ARB_direct_state_access = false;
    bool has_ARB_separate_shader_objects = false;
    bool has_ARB_vertex_attrib_binding = false;

    OpenGLState state;

    RasterizerCacheOpenGL res_cache;
    ShaderCacheOpenGL shader_cache;

    Core::Frontend::EmuWindow& emu_window;

    ScreenInfo& screen_info;

    std::unique_ptr<GLShader::ProgramManager> shader_program_manager;
    OGLVertexArray sw_vao;
    OGLVertexArray hw_vao;

    std::array<SamplerInfo, GLShader::NumTextureSamplers> texture_samplers;

    static constexpr size_t STREAM_BUFFER_SIZE = 128 * 1024 * 1024;
    OGLStreamBuffer stream_buffer;
    OGLBuffer uniform_buffer;
    OGLFramebuffer framebuffer;
    GLint uniform_buffer_alignment;

    size_t CalculateVertexArraysSize() const;

    std::pair<u8*, GLintptr> SetupVertexArrays(u8* array_ptr, GLintptr buffer_offset);

    std::pair<u8*, GLintptr> SetupShaders(u8* buffer_ptr, GLintptr buffer_offset);

    std::pair<u8*, GLintptr> AlignBuffer(u8* buffer_ptr, GLintptr buffer_offset, size_t alignment);

    std::tuple<u8*, GLintptr, GLintptr> UploadMemory(u8* buffer_ptr, GLintptr buffer_offset,
                                                     Tegra::GPUVAddr gpu_addr, size_t size,
                                                     size_t alignment = 4);

    enum class AccelDraw { Disabled, Arrays, Indexed };
    AccelDraw accelerate_draw = AccelDraw::Disabled;

    using CachedPageMap = boost::icl::interval_map<u64, int>;
    CachedPageMap cached_pages;
};

} // namespace OpenGL
