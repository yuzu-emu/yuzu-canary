// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>
#include <glad/glad.h>
#include "common/common_types.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_opengl/gl_rasterizer_cache.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_shader_gen.h"
#include "video_core/renderer_opengl/gl_shader_manager.h"
#include "video_core/renderer_opengl/gl_state.h"
#include "video_core/renderer_opengl/gl_stream_buffer.h"

struct ScreenInfo;

class RasterizerOpenGL : public VideoCore::RasterizerInterface {
public:
    RasterizerOpenGL();
    ~RasterizerOpenGL() override;

    void DrawArrays() override;
    void Clear() override;
    void NotifyMaxwellRegisterChanged(u32 method) override;
    void FlushAll() override;
    void FlushRegion(Tegra::GPUVAddr addr, u64 size) override;
    void InvalidateRegion(Tegra::GPUVAddr addr, u64 size) override;
    void FlushAndInvalidateRegion(Tegra::GPUVAddr addr, u64 size) override;
    bool AccelerateDisplayTransfer(const void* config) override;
    bool AccelerateTextureCopy(const void* config) override;
    bool AccelerateFill(const void* config) override;
    bool AccelerateDisplay(const Tegra::FramebufferConfig& framebuffer, VAddr framebuffer_addr,
                           u32 pixel_stride, ScreenInfo& screen_info) override;
    bool AccelerateDrawBatch(bool is_indexed) override;

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
        u32 border_color_r;
        u32 border_color_g;
        u32 border_color_b;
        u32 border_color_a;
    };

    /// Configures the color and depth framebuffer states and returns the dirty <Color, Depth>
    /// surfaces if writing was enabled.
    std::pair<Surface, Surface> ConfigureFramebuffers();

    /// Binds the framebuffer color and depth surface
    void BindFramebufferSurfaces(const Surface& color_surface, const Surface& depth_surface,
                                 bool has_stencil);

    /*
     * Configures the current constbuffers to use for the draw command.
     * @param stage The shader stage to configure buffers for.
     * @param program The OpenGL program object that contains the specified stage.
     * @param current_bindpoint The offset at which to start counting new buffer bindpoints.
     * @param entries Vector describing the buffers that are actually used in the guest shader.
     * @returns The next available bindpoint for use in the next shader stage.
     */
    u32 SetupConstBuffers(Tegra::Engines::Maxwell3D::Regs::ShaderStage stage, GLuint program,
                          u32 current_bindpoint,
                          const std::vector<GLShader::ConstBufferEntry>& entries);

    /*
     * Configures the current textures to use for the draw command.
     * @param stage The shader stage to configure textures for.
     * @param program The OpenGL program object that contains the specified stage.
     * @param current_unit The offset at which to start counting unused texture units.
     * @param entries Vector describing the textures that are actually used in the guest shader.
     * @returns The next available bindpoint for use in the next shader stage.
     */
    u32 SetupTextures(Tegra::Engines::Maxwell3D::Regs::ShaderStage stage, GLuint program,
                      u32 current_unit, const std::vector<GLShader::SamplerEntry>& entries);

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

    /// Syncs the blend state to match the guest state
    void SyncBlendState();

    bool has_ARB_buffer_storage;
    bool has_ARB_direct_state_access;
    bool has_ARB_separate_shader_objects;
    bool has_ARB_vertex_attrib_binding;

    OpenGLState state;

    RasterizerCacheOpenGL res_cache;

    std::unique_ptr<GLShader::ProgramManager> shader_program_manager;
    OGLVertexArray sw_vao;
    OGLVertexArray hw_vao;

    std::array<SamplerInfo, GLShader::NumTextureSamplers> texture_samplers;
    std::array<std::array<OGLBuffer, Tegra::Engines::Maxwell3D::Regs::MaxConstBuffers>,
               Tegra::Engines::Maxwell3D::Regs::MaxShaderStage>
        ssbos;

    static constexpr size_t STREAM_BUFFER_SIZE = 128 * 1024 * 1024;
    std::unique_ptr<OGLStreamBuffer> stream_buffer;
    OGLBuffer uniform_buffer;
    OGLFramebuffer framebuffer;

    size_t CalculateVertexArraysSize() const;

    std::pair<u8*, GLintptr> SetupVertexArrays(u8* array_ptr, GLintptr buffer_offset);

    std::array<OGLBuffer, Tegra::Engines::Maxwell3D::Regs::MaxShaderStage> uniform_buffers;

    void SetupShaders(u8* buffer_ptr, GLintptr buffer_offset);

    enum class AccelDraw { Disabled, Arrays, Indexed };
    AccelDraw accelerate_draw;
};
