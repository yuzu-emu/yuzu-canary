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
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_opengl/gl_rasterizer_cache.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_shader_gen.h"
#include "video_core/renderer_opengl/gl_state.h"
#include "video_core/renderer_opengl/gl_stream_buffer.h"

struct ScreenInfo;

class RasterizerOpenGL : public VideoCore::RasterizerInterface {
public:
    RasterizerOpenGL();
    ~RasterizerOpenGL() override;

    void DrawTriangles() override;
    void NotifyMaxwellRegisterChanged(u32 id) override;
    void FlushAll() override;
    void FlushRegion(VAddr addr, u32 size) override;
    void InvalidateRegion(VAddr addr, u32 size) override;
    void FlushAndInvalidateRegion(VAddr addr, u32 size) override;
    bool AccelerateDisplayTransfer(const void* config) override;
    bool AccelerateTextureCopy(const void* config) override;
    bool AccelerateFill(const void* config) override;
    bool AccelerateDisplay(const Tegra::FramebufferConfig& framebuffer, VAddr framebuffer_addr,
                           u32 pixel_stride, ScreenInfo& screen_info) override;
    bool AccelerateDrawBatch(bool is_indexed) override;

    /// OpenGL shader generated for a given Maxwell register state
    struct MaxwellShader {
        /// OpenGL shader resource
        OGLShader shader;
    };

    struct VertexShader {
        OGLShader shader;
    };

    struct FragmentShader {
        OGLShader shader;
    };

    /// Uniform structure for the Uniform Buffer Object, all vectors must be 16-byte aligned
    // NOTE: Always keep a vec4 at the end. The GL spec is not clear wether the alignment at
    //       the end of a uniform block is included in UNIFORM_BLOCK_DATA_SIZE or not.
    //       Not following that rule will cause problems on some AMD drivers.
    struct UniformData {};

    // static_assert(
    //    sizeof(UniformData) == 0x460,
    //    "The size of the UniformData structure has changed, update the structure in the shader");
    static_assert(sizeof(UniformData) < 16384,
                  "UniformData structure must be less than 16kb as per the OpenGL spec");

    struct VSUniformData {};
    // static_assert(
    //    sizeof(VSUniformData) == 1856,
    //    "The size of the VSUniformData structure has changed, update the structure in the
    //    shader");
    static_assert(sizeof(VSUniformData) < 16384,
                  "VSUniformData structure must be less than 16kb as per the OpenGL spec");

    struct FSUniformData {};
    // static_assert(
    //    sizeof(FSUniformData) == 1856,
    //    "The size of the FSUniformData structure has changed, update the structure in the
    //    shader");
    static_assert(sizeof(FSUniformData) < 16384,
                  "FSUniformData structure must be less than 16kb as per the OpenGL spec");

private:
    struct SamplerInfo {};

    /// Syncs the clip enabled status to match the guest state
    void SyncClipEnabled();

    /// Syncs the clip coefficients to match the guest state
    void SyncClipCoef();

    /// Sets the OpenGL shader in accordance with the current guest state
    void SetShader();

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

    /// Shader used for test renderering - to be removed once we have emulated shaders
    MaxwellShader test_shader{};

    const MaxwellShader* current_shader{};
    bool shader_dirty{};

    struct {
        UniformData data;
        bool dirty;
    } uniform_block_data = {};

    OGLPipeline pipeline;
    OGLVertexArray sw_vao;
    OGLVertexArray hw_vao;
    std::array<bool, 16> hw_vao_enabled_attributes;

    std::array<SamplerInfo, 3> texture_samplers;
    static constexpr size_t VERTEX_BUFFER_SIZE = 128 * 1024 * 1024;
    std::unique_ptr<OGLStreamBuffer> vertex_buffer;
    OGLBuffer uniform_buffer;
    OGLFramebuffer framebuffer;

    static constexpr size_t STREAM_BUFFER_SIZE = 4 * 1024 * 1024;
    std::unique_ptr<OGLStreamBuffer> stream_buffer;

    GLsizeiptr vs_input_size;

    void AnalyzeVertexArray(bool is_indexed);
    void SetupVertexArray(u8* array_ptr, GLintptr buffer_offset);

    OGLBuffer vs_uniform_buffer;
    std::unordered_map<GLShader::MaxwellVSConfig, VertexShader*> vs_shader_map;
    std::unordered_map<std::string, VertexShader> vs_shader_cache;
    OGLShader vs_default_shader;

    void SetupVertexShader(VSUniformData* ub_ptr, GLintptr buffer_offset);

    OGLBuffer fs_uniform_buffer;
    std::unordered_map<GLShader::MaxwellFSConfig, FragmentShader*> fs_shader_map;
    std::unordered_map<std::string, FragmentShader> fs_shader_cache;
    OGLShader fs_default_shader;

    void SetupFragmentShader(FSUniformData* ub_ptr, GLintptr buffer_offset);

    enum class AccelDraw { Disabled, Arrays, Indexed };
    AccelDraw accelerate_draw;
};
