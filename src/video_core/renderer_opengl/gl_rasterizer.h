// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>
#include <glad/glad.h>
#include "common/bit_field.h"
#include "common/common_types.h"
#include "common/hash.h"
#include "common/vector_math.h"
#include "video_core/engines/maxwell_3d.h"
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
    void NotifyMaxwellRegisterChanged(u32 method) override;
    void FlushAll() override;
    void FlushRegion(VAddr addr, u64 size) override;
    void InvalidateRegion(VAddr addr, u64 size) override;
    void FlushAndInvalidateRegion(VAddr addr, u64 size) override;
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

    /// Binds the framebuffer color and depth surface
    void BindFramebufferSurfaces(const Surface& color_surface, const Surface& depth_surface,
                                 bool has_stencil);

    /// Binds the required textures to OpenGL before drawing a batch.
    void BindTextures();

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

    /// Syncs the viewport to match the guest state
    void SyncViewport(const MathUtil::Rectangle<u32>& surfaces_rect, u16 res_scale);

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

    /// Syncs the blend enabled status to match the guest state
    void SyncBlendEnabled();

    /// Syncs the blend functions to match the guest state
    void SyncBlendFuncs();

    /// Syncs the blend color to match the guest state
    void SyncBlendColor();

    bool has_ARB_buffer_storage;
    bool has_ARB_direct_state_access;
    bool has_ARB_separate_shader_objects;
    bool has_ARB_vertex_attrib_binding;

    OpenGLState state;

    RasterizerCacheOpenGL res_cache;

    std::unique_ptr<GLShader::ProgramManager> shader_program_manager;
    OGLVertexArray sw_vao;
    OGLVertexArray hw_vao;
    std::array<bool, 16> hw_vao_enabled_attributes;

    std::array<SamplerInfo, GLShader::NumTextureSamplers> texture_samplers;
    std::array<std::array<OGLBuffer, Tegra::Engines::Maxwell3D::Regs::MaxConstBuffers>,
               Tegra::Engines::Maxwell3D::Regs::MaxShaderStage>
        ssbos;

    static constexpr size_t VERTEX_BUFFER_SIZE = 128 * 1024 * 1024;
    std::unique_ptr<OGLStreamBuffer> vertex_buffer;
    OGLBuffer uniform_buffer;
    OGLFramebuffer framebuffer;

    static constexpr size_t STREAM_BUFFER_SIZE = 4 * 1024 * 1024;
    std::unique_ptr<OGLStreamBuffer> stream_buffer;

    GLsizeiptr vs_input_size;

    void SetupVertexArray(u8* array_ptr, GLintptr buffer_offset);

    std::array<OGLBuffer, Tegra::Engines::Maxwell3D::Regs::MaxShaderStage> uniform_buffers;

    void SetupShaders(u8* buffer_ptr, GLintptr buffer_offset, size_t ptr_pos);

    enum class AccelDraw { Disabled, Arrays, Indexed };
    AccelDraw accelerate_draw;
};
