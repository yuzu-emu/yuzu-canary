// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include <boost/icl/interval_map.hpp>
#include <boost/range/iterator_range.hpp>
#include <glad/glad.h>

#include "common/common_types.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_cache.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_opengl/gl_buffer_cache.h"
#include "video_core/renderer_opengl/gl_primitive_assembler.h"
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
struct DrawParameters;

class RasterizerOpenGL : public VideoCore::RasterizerInterface {
public:
    explicit RasterizerOpenGL(Core::Frontend::EmuWindow& renderer, ScreenInfo& info);
    ~RasterizerOpenGL() override;

    void DrawArrays() override;
    void Clear() override;
    void FlushAll() override;
    void FlushRegion(VAddr addr, u64 size) override;
    void InvalidateRegion(VAddr addr, u64 size) override;
    void FlushAndInvalidateRegion(VAddr addr, u64 size) override;
    bool AccelerateSurfaceCopy(const Tegra::Engines::Fermi2D::Regs::Surface& src,
                               const Tegra::Engines::Fermi2D::Regs::Surface& dst) override;
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
    static constexpr std::size_t MaxConstbufferSize = 0x10000;
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
        void SyncWithConfig(const Tegra::Texture::TSCEntry& info);

    private:
        Tegra::Texture::TextureFilter mag_filter = Tegra::Texture::TextureFilter::Nearest;
        Tegra::Texture::TextureFilter min_filter = Tegra::Texture::TextureFilter::Nearest;
        Tegra::Texture::TextureMipmapFilter mip_filter = Tegra::Texture::TextureMipmapFilter::None;
        Tegra::Texture::WrapMode wrap_u = Tegra::Texture::WrapMode::ClampToEdge;
        Tegra::Texture::WrapMode wrap_v = Tegra::Texture::WrapMode::ClampToEdge;
        Tegra::Texture::WrapMode wrap_p = Tegra::Texture::WrapMode::ClampToEdge;
        bool uses_depth_compare = false;
        Tegra::Texture::DepthCompareFunc depth_compare_func =
            Tegra::Texture::DepthCompareFunc::Always;
        GLvec4 border_color = {};
        float min_lod = 0.0f;
        float max_lod = 16.0f;
        float lod_bias = 0.0f;
        float max_anisotropic = 1.0f;
    };

    /**
     * Configures the color and depth framebuffer states.
     * @param use_color_fb If true, configure color framebuffers.
     * @param using_depth_fb If true, configure the depth/stencil framebuffer.
     * @param preserve_contents If true, tries to preserve data from a previously used framebuffer.
     * @param single_color_target Specifies if a single color buffer target should be used.
     */
    void ConfigureFramebuffers(OpenGLState& current_state, bool use_color_fb = true,
                               bool using_depth_fb = true, bool preserve_contents = true,
                               std::optional<std::size_t> single_color_target = {});

    /*
     * Configures the current constbuffers to use for the draw command.
     * @param stage The shader stage to configure buffers for.
     * @param shader The shader object that contains the specified stage.
     * @param current_bindpoint The offset at which to start counting new buffer bindpoints.
     * @returns The next available bindpoint for use in the next shader stage.
     */
    u32 SetupConstBuffers(Tegra::Engines::Maxwell3D::Regs::ShaderStage stage, Shader& shader,
                          GLenum primitive_mode, u32 current_bindpoint);

    /*
     * Configures the current textures to use for the draw command.
     * @param stage The shader stage to configure textures for.
     * @param shader The shader object that contains the specified stage.
     * @param current_unit The offset at which to start counting unused texture units.
     * @returns The next available bindpoint for use in the next shader stage.
     */
    u32 SetupTextures(Tegra::Engines::Maxwell3D::Regs::ShaderStage stage, Shader& shader,
                      GLenum primitive_mode, u32 current_unit);

    /// Syncs the viewport and depth range to match the guest state
    void SyncViewport(OpenGLState& current_state);

    /// Syncs the clip enabled status to match the guest state
    void SyncClipEnabled();

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

    /// Check asserts for alpha testing.
    void CheckAlphaTests();

    bool has_ARB_direct_state_access = false;
    bool has_ARB_multi_bind = false;

    OpenGLState state;

    RasterizerCacheOpenGL res_cache;
    ShaderCacheOpenGL shader_cache;

    Core::Frontend::EmuWindow& emu_window;

    ScreenInfo& screen_info;

    std::unique_ptr<GLShader::ProgramManager> shader_program_manager;
    std::map<std::array<Tegra::Engines::Maxwell3D::Regs::VertexAttribute,
                        Tegra::Engines::Maxwell3D::Regs::NumVertexAttributes>,
             OGLVertexArray>
        vertex_array_cache;

    std::array<SamplerInfo, Tegra::Engines::Maxwell3D::Regs::NumTextureSamplers> texture_samplers;

    static constexpr std::size_t STREAM_BUFFER_SIZE = 128 * 1024 * 1024;
    OGLBufferCache buffer_cache;
    OGLFramebuffer framebuffer;
    PrimitiveAssembler primitive_assembler{buffer_cache};
    GLint uniform_buffer_alignment;

    std::size_t CalculateVertexArraysSize() const;

    std::size_t CalculateIndexBufferSize() const;

    void SetupVertexFormat();
    void SetupVertexBuffer();

    DrawParameters SetupDraw();

    void SetupShaders(GLenum primitive_mode);

    enum class AccelDraw { Disabled, Arrays, Indexed };
    AccelDraw accelerate_draw = AccelDraw::Disabled;

    using CachedPageMap = boost::icl::interval_map<u64, int>;
    CachedPageMap cached_pages;
};

} // namespace OpenGL
