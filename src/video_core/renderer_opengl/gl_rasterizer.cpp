// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <glad/glad.h>
#include "common/alignment.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "common/math_util.h"
#include "common/microprofile.h"
#include "common/scope_exit.h"
#include "core/core.h"
#include "core/frontend/emu_window.h"
#include "core/hle/kernel/process.h"
#include "core/settings.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/renderer_opengl/gl_shader_gen.h"
#include "video_core/renderer_opengl/maxwell_to_gl.h"
#include "video_core/renderer_opengl/renderer_opengl.h"
#include "video_core/video_core.h"

namespace OpenGL {

using Maxwell = Tegra::Engines::Maxwell3D::Regs;
using PixelFormat = VideoCore::Surface::PixelFormat;
using SurfaceType = VideoCore::Surface::SurfaceType;

MICROPROFILE_DEFINE(OpenGL_VAO, "OpenGL", "Vertex Format Setup", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_VB, "OpenGL", "Vertex Buffer Setup", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_Shader, "OpenGL", "Shader Setup", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_UBO, "OpenGL", "Const Buffer Setup", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_Index, "OpenGL", "Index Buffer Setup", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_Texture, "OpenGL", "Texture Setup", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_Framebuffer, "OpenGL", "Framebuffer Setup", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_Drawing, "OpenGL", "Drawing", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_Blits, "OpenGL", "Blits", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_CacheManagement, "OpenGL", "Cache Mgmt", MP_RGB(100, 255, 100));
MICROPROFILE_DEFINE(OpenGL_PrimitiveAssembly, "OpenGL", "Prim Asmbl", MP_RGB(255, 100, 100));

struct DrawParameters {
    GLenum primitive_mode;
    GLsizei count;
    GLint current_instance;
    bool use_indexed;

    GLint vertex_first;

    GLenum index_format;
    GLint base_vertex;
    GLintptr index_buffer_offset;

    void DispatchDraw() const {
        if (use_indexed) {
            const auto index_buffer_ptr = reinterpret_cast<const void*>(index_buffer_offset);
            if (current_instance > 0) {
                glDrawElementsInstancedBaseVertexBaseInstance(primitive_mode, count, index_format,
                                                              index_buffer_ptr, 1, base_vertex,
                                                              current_instance);
            } else {
                glDrawElementsBaseVertex(primitive_mode, count, index_format, index_buffer_ptr,
                                         base_vertex);
            }
        } else {
            if (current_instance > 0) {
                glDrawArraysInstancedBaseInstance(primitive_mode, vertex_first, count, 1,
                                                  current_instance);
            } else {
                glDrawArrays(primitive_mode, vertex_first, count);
            }
        }
    }
};

RasterizerOpenGL::RasterizerOpenGL(Core::Frontend::EmuWindow& window, ScreenInfo& info)
    : res_cache{*this}, shader_cache{*this}, emu_window{window}, screen_info{info},
      buffer_cache(*this, STREAM_BUFFER_SIZE) {
    // Create sampler objects
    for (std::size_t i = 0; i < texture_samplers.size(); ++i) {
        texture_samplers[i].Create();
        state.texture_units[i].sampler = texture_samplers[i].sampler.handle;
    }

    GLint ext_num;
    glGetIntegerv(GL_NUM_EXTENSIONS, &ext_num);
    for (GLint i = 0; i < ext_num; i++) {
        const std::string_view extension{
            reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i))};

        if (extension == "GL_ARB_direct_state_access") {
            has_ARB_direct_state_access = true;
        } else if (extension == "GL_ARB_multi_bind") {
            has_ARB_multi_bind = true;
        }
    }

    OpenGLState::ApplyDefaultState();

    // Create render framebuffer
    framebuffer.Create();

    shader_program_manager = std::make_unique<GLShader::ProgramManager>();
    state.draw.shader_program = 0;
    state.Apply();

    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &uniform_buffer_alignment);

    LOG_CRITICAL(Render_OpenGL, "Sync fixed function OpenGL state here!");
}

RasterizerOpenGL::~RasterizerOpenGL() {}

void RasterizerOpenGL::SetupVertexFormat() {
    auto& gpu = Core::System::GetInstance().GPU().Maxwell3D();
    const auto& regs = gpu.regs;

    if (!gpu.dirty_flags.vertex_attrib_format)
        return;
    gpu.dirty_flags.vertex_attrib_format = false;

    MICROPROFILE_SCOPE(OpenGL_VAO);

    auto [iter, is_cache_miss] = vertex_array_cache.try_emplace(regs.vertex_attrib_format);
    auto& VAO = iter->second;

    if (is_cache_miss) {
        VAO.Create();
        state.draw.vertex_array = VAO.handle;
        state.ApplyVertexBufferState();

        // The index buffer binding is stored within the VAO. Stupid OpenGL, but easy to work
        // around.
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer_cache.GetHandle());

        // Use the vertex array as-is, assumes that the data is formatted correctly for OpenGL.
        // Enables the first 16 vertex attributes always, as we don't know which ones are actually
        // used until shader time. Note, Tegra technically supports 32, but we're capping this to 16
        // for now to avoid OpenGL errors.
        // TODO(Subv): Analyze the shader to identify which attributes are actually used and don't
        // assume every shader uses them all.
        for (unsigned index = 0; index < 16; ++index) {
            const auto& attrib = regs.vertex_attrib_format[index];

            // Ignore invalid attributes.
            if (!attrib.IsValid())
                continue;

            const auto& buffer = regs.vertex_array[attrib.buffer];
            LOG_TRACE(HW_GPU,
                      "vertex attrib {}, count={}, size={}, type={}, offset={}, normalize={}",
                      index, attrib.ComponentCount(), attrib.SizeString(), attrib.TypeString(),
                      attrib.offset.Value(), attrib.IsNormalized());

            ASSERT(buffer.IsEnabled());

            glEnableVertexAttribArray(index);
            if (attrib.type == Tegra::Engines::Maxwell3D::Regs::VertexAttribute::Type::SignedInt ||
                attrib.type ==
                    Tegra::Engines::Maxwell3D::Regs::VertexAttribute::Type::UnsignedInt) {
                glVertexAttribIFormat(index, attrib.ComponentCount(),
                                      MaxwellToGL::VertexType(attrib), attrib.offset);
            } else {
                glVertexAttribFormat(index, attrib.ComponentCount(),
                                     MaxwellToGL::VertexType(attrib),
                                     attrib.IsNormalized() ? GL_TRUE : GL_FALSE, attrib.offset);
            }
            glVertexAttribBinding(index, attrib.buffer);
        }
    }
    state.draw.vertex_array = VAO.handle;
    state.ApplyVertexBufferState();

    // Rebinding the VAO invalidates the vertex buffer bindings.
    gpu.dirty_flags.vertex_array = 0xFFFFFFFF;
}

void RasterizerOpenGL::SetupVertexBuffer() {
    auto& gpu = Core::System::GetInstance().GPU().Maxwell3D();
    const auto& regs = gpu.regs;

    if (!gpu.dirty_flags.vertex_array)
        return;

    MICROPROFILE_SCOPE(OpenGL_VB);

    // Upload all guest vertex arrays sequentially to our buffer
    for (u32 index = 0; index < Maxwell::NumVertexArrays; ++index) {
        if (~gpu.dirty_flags.vertex_array & (1u << index))
            continue;

        const auto& vertex_array = regs.vertex_array[index];
        if (!vertex_array.IsEnabled())
            continue;

        Tegra::GPUVAddr start = vertex_array.StartAddress();
        const Tegra::GPUVAddr end = regs.vertex_array_limit[index].LimitAddress();

        ASSERT(end > start);
        const u64 size = end - start + 1;
        const GLintptr vertex_buffer_offset = buffer_cache.UploadMemory(start, size);

        // Bind the vertex array to the buffer at the current offset.
        glBindVertexBuffer(index, buffer_cache.GetHandle(), vertex_buffer_offset,
                           vertex_array.stride);

        if (regs.instanced_arrays.IsInstancingEnabled(index) && vertex_array.divisor != 0) {
            // Enable vertex buffer instancing with the specified divisor.
            glVertexBindingDivisor(index, vertex_array.divisor);
        } else {
            // Disable the vertex buffer instancing.
            glVertexBindingDivisor(index, 0);
        }
    }

    // Implicit set by glBindVertexBuffer. Stupid glstate handling...
    state.draw.vertex_buffer = buffer_cache.GetHandle();

    gpu.dirty_flags.vertex_array = 0;
}

DrawParameters RasterizerOpenGL::SetupDraw() {
    const auto& gpu = Core::System::GetInstance().GPU().Maxwell3D();
    const auto& regs = gpu.regs;
    const bool is_indexed = accelerate_draw == AccelDraw::Indexed;

    DrawParameters params{};
    params.current_instance = gpu.state.current_instance;

    if (regs.draw.topology == Maxwell::PrimitiveTopology::Quads) {
        MICROPROFILE_SCOPE(OpenGL_PrimitiveAssembly);

        params.use_indexed = true;
        params.primitive_mode = GL_TRIANGLES;

        if (is_indexed) {
            params.index_format = MaxwellToGL::IndexFormat(regs.index_array.format);
            params.count = (regs.index_array.count / 4) * 6;
            params.index_buffer_offset = primitive_assembler.MakeQuadIndexed(
                regs.index_array.IndexStart(), regs.index_array.FormatSizeInBytes(),
                regs.index_array.count);
            params.base_vertex = static_cast<GLint>(regs.vb_element_base);
        } else {
            // MakeQuadArray always generates u32 indexes
            params.index_format = GL_UNSIGNED_INT;
            params.count = (regs.vertex_buffer.count / 4) * 6;
            params.index_buffer_offset =
                primitive_assembler.MakeQuadArray(regs.vertex_buffer.first, params.count);
        }
        return params;
    }

    params.use_indexed = is_indexed;
    params.primitive_mode = MaxwellToGL::PrimitiveTopology(regs.draw.topology);

    if (is_indexed) {
        MICROPROFILE_SCOPE(OpenGL_Index);
        params.index_format = MaxwellToGL::IndexFormat(regs.index_array.format);
        params.count = regs.index_array.count;
        params.index_buffer_offset =
            buffer_cache.UploadMemory(regs.index_array.IndexStart(), CalculateIndexBufferSize());
        params.base_vertex = static_cast<GLint>(regs.vb_element_base);
    } else {
        params.count = regs.vertex_buffer.count;
        params.vertex_first = regs.vertex_buffer.first;
    }
    return params;
}

void RasterizerOpenGL::SetupShaders(GLenum primitive_mode) {
    MICROPROFILE_SCOPE(OpenGL_Shader);
    const auto& gpu = Core::System::GetInstance().GPU().Maxwell3D();

    // Next available bindpoints to use when uploading the const buffers and textures to the GLSL
    // shaders. The constbuffer bindpoint starts after the shader stage configuration bind points.
    u32 current_constbuffer_bindpoint = Tegra::Engines::Maxwell3D::Regs::MaxShaderStage;
    u32 current_texture_bindpoint = 0;

    for (std::size_t index = 0; index < Maxwell::MaxShaderProgram; ++index) {
        const auto& shader_config = gpu.regs.shader_config[index];
        const Maxwell::ShaderProgram program{static_cast<Maxwell::ShaderProgram>(index)};

        // Skip stages that are not enabled
        if (!gpu.regs.IsShaderConfigEnabled(index)) {
            switch (program) {
            case Maxwell::ShaderProgram::Geometry:
                shader_program_manager->UseTrivialGeometryShader();
                break;
            }
            continue;
        }

        const std::size_t stage{index == 0 ? 0 : index - 1}; // Stage indices are 0 - 5

        GLShader::MaxwellUniformData ubo{};
        ubo.SetFromRegs(gpu.state.shader_stages[stage]);
        const GLintptr offset = buffer_cache.UploadHostMemory(
            &ubo, sizeof(ubo), static_cast<std::size_t>(uniform_buffer_alignment));

        // Bind the buffer
        glBindBufferRange(GL_UNIFORM_BUFFER, static_cast<GLuint>(stage), buffer_cache.GetHandle(),
                          offset, static_cast<GLsizeiptr>(sizeof(ubo)));

        Shader shader{shader_cache.GetStageProgram(program)};

        switch (program) {
        case Maxwell::ShaderProgram::VertexA:
        case Maxwell::ShaderProgram::VertexB: {
            shader_program_manager->UseProgrammableVertexShader(
                shader->GetProgramHandle(primitive_mode));
            break;
        }
        case Maxwell::ShaderProgram::Geometry: {
            shader_program_manager->UseProgrammableGeometryShader(
                shader->GetProgramHandle(primitive_mode));
            break;
        }
        case Maxwell::ShaderProgram::Fragment: {
            shader_program_manager->UseProgrammableFragmentShader(
                shader->GetProgramHandle(primitive_mode));
            break;
        }
        default:
            LOG_CRITICAL(HW_GPU, "Unimplemented shader index={}, enable={}, offset=0x{:08X}", index,
                         shader_config.enable.Value(), shader_config.offset);
            UNREACHABLE();
        }

        // Configure the const buffers for this shader stage.
        current_constbuffer_bindpoint =
            SetupConstBuffers(static_cast<Maxwell::ShaderStage>(stage), shader, primitive_mode,
                              current_constbuffer_bindpoint);

        // Configure the textures for this shader stage.
        current_texture_bindpoint = SetupTextures(static_cast<Maxwell::ShaderStage>(stage), shader,
                                                  primitive_mode, current_texture_bindpoint);

        // When VertexA is enabled, we have dual vertex shaders
        if (program == Maxwell::ShaderProgram::VertexA) {
            // VertexB was combined with VertexA, so we skip the VertexB iteration
            index++;
        }
    }
}

std::size_t RasterizerOpenGL::CalculateVertexArraysSize() const {
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;

    std::size_t size = 0;
    for (u32 index = 0; index < Maxwell::NumVertexArrays; ++index) {
        if (!regs.vertex_array[index].IsEnabled())
            continue;

        const Tegra::GPUVAddr start = regs.vertex_array[index].StartAddress();
        const Tegra::GPUVAddr end = regs.vertex_array_limit[index].LimitAddress();

        ASSERT(end > start);
        size += end - start + 1;
    }

    return size;
}

std::size_t RasterizerOpenGL::CalculateIndexBufferSize() const {
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;

    return static_cast<std::size_t>(regs.index_array.count) *
           static_cast<std::size_t>(regs.index_array.FormatSizeInBytes());
}

bool RasterizerOpenGL::AccelerateDrawBatch(bool is_indexed) {
    accelerate_draw = is_indexed ? AccelDraw::Indexed : AccelDraw::Arrays;
    DrawArrays();
    return true;
}

template <typename Map, typename Interval>
static constexpr auto RangeFromInterval(Map& map, const Interval& interval) {
    return boost::make_iterator_range(map.equal_range(interval));
}

void RasterizerOpenGL::UpdatePagesCachedCount(VAddr addr, u64 size, int delta) {
    const u64 page_start{addr >> Memory::PAGE_BITS};
    const u64 page_end{(addr + size + Memory::PAGE_SIZE - 1) >> Memory::PAGE_BITS};

    // Interval maps will erase segments if count reaches 0, so if delta is negative we have to
    // subtract after iterating
    const auto pages_interval = CachedPageMap::interval_type::right_open(page_start, page_end);
    if (delta > 0)
        cached_pages.add({pages_interval, delta});

    for (const auto& pair : RangeFromInterval(cached_pages, pages_interval)) {
        const auto interval = pair.first & pages_interval;
        const int count = pair.second;

        const VAddr interval_start_addr = boost::icl::first(interval) << Memory::PAGE_BITS;
        const VAddr interval_end_addr = boost::icl::last_next(interval) << Memory::PAGE_BITS;
        const u64 interval_size = interval_end_addr - interval_start_addr;

        if (delta > 0 && count == delta)
            Memory::RasterizerMarkRegionCached(interval_start_addr, interval_size, true);
        else if (delta < 0 && count == -delta)
            Memory::RasterizerMarkRegionCached(interval_start_addr, interval_size, false);
        else
            ASSERT(count >= 0);
    }

    if (delta < 0)
        cached_pages.add({pages_interval, delta});
}

void RasterizerOpenGL::ConfigureFramebuffers(OpenGLState& current_state, bool using_color_fb,
                                             bool using_depth_fb, bool preserve_contents,
                                             std::optional<std::size_t> single_color_target) {
    MICROPROFILE_SCOPE(OpenGL_Framebuffer);
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;

    Surface depth_surface;
    if (using_depth_fb) {
        depth_surface = res_cache.GetDepthBufferSurface(preserve_contents);
    }

    // TODO(bunnei): Figure out how the below register works. According to envytools, this should be
    // used to enable multiple render targets. However, it is left unset on all games that I have
    // tested.
    ASSERT_MSG(regs.rt_separate_frag_data == 0, "Unimplemented");

    // Bind the framebuffer surfaces
    current_state.draw.draw_framebuffer = framebuffer.handle;
    current_state.ApplyFramebufferState();
    current_state.framebuffer_srgb.enabled = regs.framebuffer_srgb != 0;

    if (using_color_fb) {
        if (single_color_target) {
            // Used when just a single color attachment is enabled, e.g. for clearing a color buffer
            Surface color_surface =
                res_cache.GetColorBufferSurface(*single_color_target, preserve_contents);

            if (color_surface) {
                // Assume that a surface will be written to if it is used as a framebuffer, even if
                // the shader doesn't actually write to it.
                color_surface->MarkAsModified(true, res_cache);
                // Workaround for and issue in nvidia drivers
                // https://devtalk.nvidia.com/default/topic/776591/opengl/gl_framebuffer_srgb-functions-incorrectly/
                state.framebuffer_srgb.enabled |= color_surface->GetSurfaceParams().srgb_conversion;
            }

            glFramebufferTexture2D(
                GL_DRAW_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(*single_color_target), GL_TEXTURE_2D,
                color_surface != nullptr ? color_surface->Texture().handle : 0, 0);
            glDrawBuffer(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(*single_color_target));
        } else {
            // Multiple color attachments are enabled
            std::array<GLenum, Maxwell::NumRenderTargets> buffers;
            for (std::size_t index = 0; index < Maxwell::NumRenderTargets; ++index) {
                Surface color_surface = res_cache.GetColorBufferSurface(index, preserve_contents);

                if (color_surface) {
                    // Assume that a surface will be written to if it is used as a framebuffer, even
                    // if the shader doesn't actually write to it.
                    color_surface->MarkAsModified(true, res_cache);
                    // Enable sRGB only for supported formats
                    // Workaround for and issue in nvidia drivers
                    // https://devtalk.nvidia.com/default/topic/776591/opengl/gl_framebuffer_srgb-functions-incorrectly/
                    state.framebuffer_srgb.enabled |=
                        color_surface->GetSurfaceParams().srgb_conversion;
                }

                buffers[index] = GL_COLOR_ATTACHMENT0 + regs.rt_control.GetMap(index);
                glFramebufferTexture2D(
                    GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(index),
                    GL_TEXTURE_2D, color_surface != nullptr ? color_surface->Texture().handle : 0,
                    0);
            }
            glDrawBuffers(regs.rt_control.count, buffers.data());
        }
    } else {
        // No color attachments are enabled - zero out all of them
        for (std::size_t index = 0; index < Maxwell::NumRenderTargets; ++index) {
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,
                                   GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(index), GL_TEXTURE_2D,
                                   0, 0);
        }
        glDrawBuffer(GL_NONE);
    }

    if (depth_surface) {
        // Assume that a surface will be written to if it is used as a framebuffer, even if
        // the shader doesn't actually write to it.
        depth_surface->MarkAsModified(true, res_cache);

        if (regs.stencil_enable) {
            // Attach both depth and stencil
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                                   depth_surface->Texture().handle, 0);
        } else {
            // Attach depth
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                                   depth_surface->Texture().handle, 0);
            // Clear stencil attachment
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        }
    } else {
        // Clear both depth and stencil attachment
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0,
                               0);
    }
    SyncViewport(current_state);
}

void RasterizerOpenGL::Clear() {
    const auto prev_state{state};
    SCOPE_EXIT({ prev_state.Apply(); });

    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;
    bool use_color{};
    bool use_depth{};
    bool use_stencil{};

    OpenGLState clear_state;
    if (regs.clear_buffers.R || regs.clear_buffers.G || regs.clear_buffers.B ||
        regs.clear_buffers.A) {
        use_color = true;
    }
    if (use_color) {
        clear_state.color_mask[0].red_enabled = regs.clear_buffers.R ? GL_TRUE : GL_FALSE;
        clear_state.color_mask[0].green_enabled = regs.clear_buffers.G ? GL_TRUE : GL_FALSE;
        clear_state.color_mask[0].blue_enabled = regs.clear_buffers.B ? GL_TRUE : GL_FALSE;
        clear_state.color_mask[0].alpha_enabled = regs.clear_buffers.A ? GL_TRUE : GL_FALSE;
    }
    if (regs.clear_buffers.Z) {
        ASSERT_MSG(regs.zeta_enable != 0, "Tried to clear Z but buffer is not enabled!");
        use_depth = true;

        // Always enable the depth write when clearing the depth buffer. The depth write mask is
        // ignored when clearing the buffer in the Switch, but OpenGL obeys it so we set it to
        // true.
        clear_state.depth.test_enabled = true;
        clear_state.depth.test_func = GL_ALWAYS;
    }
    if (regs.clear_buffers.S) {
        ASSERT_MSG(regs.zeta_enable != 0, "Tried to clear stencil but buffer is not enabled!");
        use_stencil = true;
        clear_state.stencil.test_enabled = true;
        if (regs.clear_flags.stencil) {
            // Stencil affects the clear so fill it with the used masks
            clear_state.stencil.front.test_func = GL_ALWAYS;
            clear_state.stencil.front.test_mask = regs.stencil_front_func_mask;
            clear_state.stencil.front.action_stencil_fail = GL_KEEP;
            clear_state.stencil.front.action_depth_fail = GL_KEEP;
            clear_state.stencil.front.action_depth_pass = GL_KEEP;
            clear_state.stencil.front.write_mask = regs.stencil_front_mask;
            if (regs.stencil_two_side_enable) {
                clear_state.stencil.back.test_func = GL_ALWAYS;
                clear_state.stencil.back.test_mask = regs.stencil_back_func_mask;
                clear_state.stencil.back.action_stencil_fail = GL_KEEP;
                clear_state.stencil.back.action_depth_fail = GL_KEEP;
                clear_state.stencil.back.action_depth_pass = GL_KEEP;
                clear_state.stencil.back.write_mask = regs.stencil_back_mask;
            } else {
                clear_state.stencil.back.test_func = GL_ALWAYS;
                clear_state.stencil.back.test_mask = 0xFFFFFFFF;
                clear_state.stencil.back.write_mask = 0xFFFFFFFF;
                clear_state.stencil.back.action_stencil_fail = GL_KEEP;
                clear_state.stencil.back.action_depth_fail = GL_KEEP;
                clear_state.stencil.back.action_depth_pass = GL_KEEP;
            }
        }
    }

    if (!use_color && !use_depth && !use_stencil) {
        // No color surface nor depth/stencil surface are enabled
        return;
    }

    ScopeAcquireGLContext acquire_context{emu_window};

    ConfigureFramebuffers(clear_state, use_color, use_depth || use_stencil, false,
                          regs.clear_buffers.RT.Value());
    if (regs.clear_flags.scissor) {
        SyncScissorTest(clear_state);
    }

    if (regs.clear_flags.viewport) {
        clear_state.EmulateViewportWithScissor();
    }

    clear_state.Apply();

    if (use_color) {
        glClearBufferfv(GL_COLOR, regs.clear_buffers.RT, regs.clear_color);
    }

    if (use_depth && use_stencil) {
        glClearBufferfi(GL_DEPTH_STENCIL, 0, regs.clear_depth, regs.clear_stencil);
    } else if (use_depth) {
        glClearBufferfv(GL_DEPTH, 0, &regs.clear_depth);
    } else if (use_stencil) {
        glClearBufferiv(GL_STENCIL, 0, &regs.clear_stencil);
    }
}

void RasterizerOpenGL::DrawArrays() {
    if (accelerate_draw == AccelDraw::Disabled)
        return;

    MICROPROFILE_SCOPE(OpenGL_Drawing);
    auto& gpu = Core::System::GetInstance().GPU().Maxwell3D();
    const auto& regs = gpu.regs;

    ScopeAcquireGLContext acquire_context{emu_window};

    ConfigureFramebuffers(state);
    SyncColorMask();
    SyncFragmentColorClampState();
    SyncMultiSampleState();
    SyncDepthTestState();
    SyncStencilTestState();
    SyncBlendState();
    SyncLogicOpState();
    SyncCullMode();
    SyncPrimitiveRestart();
    SyncScissorTest(state);
    SyncClipEnabled();
    // Alpha Testing is synced on shaders.
    SyncTransformFeedback();
    SyncPointState();
    CheckAlphaTests();
    SyncPolygonOffset();
    // TODO(bunnei): Sync framebuffer_scale uniform here
    // TODO(bunnei): Sync scissorbox uniform(s) here

    // Draw the vertex batch
    const bool is_indexed = accelerate_draw == AccelDraw::Indexed;

    state.draw.vertex_buffer = buffer_cache.GetHandle();
    state.ApplyVertexBufferState();

    std::size_t buffer_size = CalculateVertexArraysSize();

    // Add space for index buffer (keeping in mind non-core primitives)
    switch (regs.draw.topology) {
    case Maxwell::PrimitiveTopology::Quads:
        buffer_size = Common::AlignUp<std::size_t>(buffer_size, 4) +
                      primitive_assembler.CalculateQuadSize(regs.vertex_buffer.count);
        break;
    default:
        if (is_indexed) {
            buffer_size = Common::AlignUp<std::size_t>(buffer_size, 4) + CalculateIndexBufferSize();
        }
        break;
    }

    // Uniform space for the 5 shader stages
    buffer_size =
        Common::AlignUp<std::size_t>(buffer_size, 4) +
        (sizeof(GLShader::MaxwellUniformData) + uniform_buffer_alignment) * Maxwell::MaxShaderStage;

    // Add space for at least 18 constant buffers
    buffer_size += Maxwell::MaxConstBuffers * (MaxConstbufferSize + uniform_buffer_alignment);

    bool invalidate = buffer_cache.Map(buffer_size);
    if (invalidate) {
        // As all cached buffers are invalidated, we need to recheck their state.
        gpu.dirty_flags.vertex_attrib_format = 0xFFFFFFFF;
    }

    SetupVertexFormat();
    SetupVertexBuffer();
    DrawParameters params = SetupDraw();
    SetupShaders(params.primitive_mode);

    buffer_cache.Unmap();

    shader_program_manager->ApplyTo(state);
    state.Apply();

    // Execute draw call
    params.DispatchDraw();

    // Disable scissor test
    state.viewports[0].scissor.enabled = false;

    accelerate_draw = AccelDraw::Disabled;

    // Unbind textures for potential future use as framebuffer attachments
    for (auto& texture_unit : state.texture_units) {
        texture_unit.Unbind();
    }
    state.Apply();
}

void RasterizerOpenGL::FlushAll() {}

void RasterizerOpenGL::FlushRegion(VAddr addr, u64 size) {
    MICROPROFILE_SCOPE(OpenGL_CacheManagement);

    if (Settings::values.use_accurate_gpu_emulation) {
        // Only flush if use_accurate_gpu_emulation is enabled, as it incurs a performance hit
        res_cache.FlushRegion(addr, size);
    }
}

void RasterizerOpenGL::InvalidateRegion(VAddr addr, u64 size) {
    MICROPROFILE_SCOPE(OpenGL_CacheManagement);
    res_cache.InvalidateRegion(addr, size);
    shader_cache.InvalidateRegion(addr, size);
    buffer_cache.InvalidateRegion(addr, size);
}

void RasterizerOpenGL::FlushAndInvalidateRegion(VAddr addr, u64 size) {
    FlushRegion(addr, size);
    InvalidateRegion(addr, size);
}

bool RasterizerOpenGL::AccelerateSurfaceCopy(const Tegra::Engines::Fermi2D::Regs::Surface& src,
                                             const Tegra::Engines::Fermi2D::Regs::Surface& dst) {
    MICROPROFILE_SCOPE(OpenGL_Blits);

    if (Settings::values.use_accurate_gpu_emulation) {
        // Skip the accelerated copy and perform a slow but more accurate copy
        return false;
    }

    res_cache.FermiCopySurface(src, dst);
    return true;
}

bool RasterizerOpenGL::AccelerateFill(const void* config) {
    UNREACHABLE();
    return true;
}

bool RasterizerOpenGL::AccelerateDisplay(const Tegra::FramebufferConfig& config,
                                         VAddr framebuffer_addr, u32 pixel_stride) {
    if (!framebuffer_addr) {
        return {};
    }

    MICROPROFILE_SCOPE(OpenGL_CacheManagement);

    const auto& surface{res_cache.TryFindFramebufferSurface(framebuffer_addr)};
    if (!surface) {
        return {};
    }

    // Verify that the cached surface is the same size and format as the requested framebuffer
    const auto& params{surface->GetSurfaceParams()};
    const auto& pixel_format{
        VideoCore::Surface::PixelFormatFromGPUPixelFormat(config.pixel_format)};
    ASSERT_MSG(params.width == config.width, "Framebuffer width is different");
    ASSERT_MSG(params.height == config.height, "Framebuffer height is different");
    ASSERT_MSG(params.pixel_format == pixel_format, "Framebuffer pixel_format is different");

    screen_info.display_texture = surface->Texture().handle;

    return true;
}

void RasterizerOpenGL::SamplerInfo::Create() {
    sampler.Create();
    mag_filter = min_filter = Tegra::Texture::TextureFilter::Linear;
    wrap_u = wrap_v = wrap_p = Tegra::Texture::WrapMode::Wrap;
    uses_depth_compare = false;
    depth_compare_func = Tegra::Texture::DepthCompareFunc::Never;

    // default is GL_LINEAR_MIPMAP_LINEAR
    glSamplerParameteri(sampler.handle, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    // Other attributes have correct defaults
    glSamplerParameteri(sampler.handle, GL_TEXTURE_COMPARE_FUNC, GL_NEVER);
}

void RasterizerOpenGL::SamplerInfo::SyncWithConfig(const Tegra::Texture::TSCEntry& config) {
    const GLuint s = sampler.handle;
    if (mag_filter != config.mag_filter) {
        mag_filter = config.mag_filter;
        glSamplerParameteri(
            s, GL_TEXTURE_MAG_FILTER,
            MaxwellToGL::TextureFilterMode(mag_filter, Tegra::Texture::TextureMipmapFilter::None));
    }
    if (min_filter != config.min_filter || mip_filter != config.mip_filter) {
        min_filter = config.min_filter;
        mip_filter = config.mip_filter;
        glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER,
                            MaxwellToGL::TextureFilterMode(min_filter, mip_filter));
    }

    if (wrap_u != config.wrap_u) {
        wrap_u = config.wrap_u;
        glSamplerParameteri(s, GL_TEXTURE_WRAP_S, MaxwellToGL::WrapMode(wrap_u));
    }
    if (wrap_v != config.wrap_v) {
        wrap_v = config.wrap_v;
        glSamplerParameteri(s, GL_TEXTURE_WRAP_T, MaxwellToGL::WrapMode(wrap_v));
    }
    if (wrap_p != config.wrap_p) {
        wrap_p = config.wrap_p;
        glSamplerParameteri(s, GL_TEXTURE_WRAP_R, MaxwellToGL::WrapMode(wrap_p));
    }

    if (uses_depth_compare != (config.depth_compare_enabled == 1)) {
        uses_depth_compare = (config.depth_compare_enabled == 1);
        if (uses_depth_compare) {
            glSamplerParameteri(s, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        } else {
            glSamplerParameteri(s, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        }
    }

    if (depth_compare_func != config.depth_compare_func) {
        depth_compare_func = config.depth_compare_func;
        glSamplerParameteri(s, GL_TEXTURE_COMPARE_FUNC,
                            MaxwellToGL::DepthCompareFunc(depth_compare_func));
    }

    GLvec4 new_border_color;
    if (config.srgb_conversion) {
        new_border_color[0] = config.srgb_border_color_r / 255.0f;
        new_border_color[1] = config.srgb_border_color_g / 255.0f;
        new_border_color[2] = config.srgb_border_color_g / 255.0f;
    } else {
        new_border_color[0] = config.border_color_r;
        new_border_color[1] = config.border_color_g;
        new_border_color[2] = config.border_color_b;
    }
    new_border_color[3] = config.border_color_a;

    if (border_color != new_border_color) {
        border_color = new_border_color;
        glSamplerParameterfv(s, GL_TEXTURE_BORDER_COLOR, border_color.data());
    }

    const float anisotropic_max = static_cast<float>(1 << config.max_anisotropy.Value());
    if (anisotropic_max != max_anisotropic) {
        max_anisotropic = anisotropic_max;
        if (GLAD_GL_ARB_texture_filter_anisotropic) {
            glSamplerParameterf(s, GL_TEXTURE_MAX_ANISOTROPY, max_anisotropic);
        } else if (GLAD_GL_EXT_texture_filter_anisotropic) {
            glSamplerParameterf(s, GL_TEXTURE_MAX_ANISOTROPY_EXT, max_anisotropic);
        }
    }
    const float lod_min = static_cast<float>(config.min_lod_clamp.Value()) / 256.0f;
    if (lod_min != min_lod) {
        min_lod = lod_min;
        glSamplerParameterf(s, GL_TEXTURE_MIN_LOD, min_lod);
    }

    const float lod_max = static_cast<float>(config.max_lod_clamp.Value()) / 256.0f;
    if (lod_max != max_lod) {
        max_lod = lod_max;
        glSamplerParameterf(s, GL_TEXTURE_MAX_LOD, max_lod);
    }
    const u32 bias = config.mip_lod_bias.Value();
    // Sign extend the 13-bit value.
    constexpr u32 mask = 1U << (13 - 1);
    const float bias_lod = static_cast<s32>((bias ^ mask) - mask) / 256.f;
    if (lod_bias != bias_lod) {
        lod_bias = bias_lod;
        glSamplerParameterf(s, GL_TEXTURE_LOD_BIAS, lod_bias);
    }
}

u32 RasterizerOpenGL::SetupConstBuffers(Maxwell::ShaderStage stage, Shader& shader,
                                        GLenum primitive_mode, u32 current_bindpoint) {
    MICROPROFILE_SCOPE(OpenGL_UBO);
    const auto& gpu = Core::System::GetInstance().GPU();
    const auto& maxwell3d = gpu.Maxwell3D();
    const auto& shader_stage = maxwell3d.state.shader_stages[static_cast<std::size_t>(stage)];
    const auto& entries = shader->GetShaderEntries().const_buffer_entries;

    constexpr u64 max_binds = Tegra::Engines::Maxwell3D::Regs::MaxConstBuffers;
    std::array<GLuint, max_binds> bind_buffers;
    std::array<GLintptr, max_binds> bind_offsets;
    std::array<GLsizeiptr, max_binds> bind_sizes;

    ASSERT_MSG(entries.size() <= max_binds, "Exceeded expected number of binding points.");

    // Upload only the enabled buffers from the 16 constbuffers of each shader stage
    for (u32 bindpoint = 0; bindpoint < entries.size(); ++bindpoint) {
        const auto& used_buffer = entries[bindpoint];
        const auto& buffer = shader_stage.const_buffers[used_buffer.GetIndex()];

        if (!buffer.enabled) {
            // With disabled buffers set values as zero to unbind them
            bind_buffers[bindpoint] = 0;
            bind_offsets[bindpoint] = 0;
            bind_sizes[bindpoint] = 0;
            continue;
        }

        std::size_t size = 0;

        if (used_buffer.IsIndirect()) {
            // Buffer is accessed indirectly, so upload the entire thing
            size = buffer.size;

            if (size > MaxConstbufferSize) {
                LOG_CRITICAL(HW_GPU, "indirect constbuffer size {} exceeds maximum {}", size,
                             MaxConstbufferSize);
                size = MaxConstbufferSize;
            }
        } else {
            // Buffer is accessed directly, upload just what we use
            size = used_buffer.GetSize() * sizeof(float);
        }

        // Align the actual size so it ends up being a multiple of vec4 to meet the OpenGL std140
        // UBO alignment requirements.
        size = Common::AlignUp(size, sizeof(GLvec4));
        ASSERT_MSG(size <= MaxConstbufferSize, "Constbuffer too big");

        GLintptr const_buffer_offset = buffer_cache.UploadMemory(
            buffer.address, size, static_cast<std::size_t>(uniform_buffer_alignment));

        // Now configure the bindpoint of the buffer inside the shader
        glUniformBlockBinding(shader->GetProgramHandle(primitive_mode),
                              shader->GetProgramResourceIndex(used_buffer),
                              current_bindpoint + bindpoint);

        // Prepare values for multibind
        bind_buffers[bindpoint] = buffer_cache.GetHandle();
        bind_offsets[bindpoint] = const_buffer_offset;
        bind_sizes[bindpoint] = size;
    }

    glBindBuffersRange(GL_UNIFORM_BUFFER, current_bindpoint, static_cast<GLsizei>(entries.size()),
                       bind_buffers.data(), bind_offsets.data(), bind_sizes.data());

    return current_bindpoint + static_cast<u32>(entries.size());
}

u32 RasterizerOpenGL::SetupTextures(Maxwell::ShaderStage stage, Shader& shader,
                                    GLenum primitive_mode, u32 current_unit) {
    MICROPROFILE_SCOPE(OpenGL_Texture);
    const auto& gpu = Core::System::GetInstance().GPU();
    const auto& maxwell3d = gpu.Maxwell3D();
    const auto& entries = shader->GetShaderEntries().texture_samplers;

    ASSERT_MSG(current_unit + entries.size() <= std::size(state.texture_units),
               "Exceeded the number of active textures.");

    for (u32 bindpoint = 0; bindpoint < entries.size(); ++bindpoint) {
        const auto& entry = entries[bindpoint];
        const u32 current_bindpoint = current_unit + bindpoint;

        // Bind the uniform to the sampler.

        glProgramUniform1i(shader->GetProgramHandle(primitive_mode),
                           shader->GetUniformLocation(entry), current_bindpoint);

        const auto texture = maxwell3d.GetStageTexture(entry.GetStage(), entry.GetOffset());

        if (!texture.enabled) {
            state.texture_units[current_bindpoint].texture = 0;
            continue;
        }

        texture_samplers[current_bindpoint].SyncWithConfig(texture.tsc);
        Surface surface = res_cache.GetTextureSurface(texture, entry);
        if (surface != nullptr) {
            state.texture_units[current_bindpoint].texture = surface->Texture().handle;
            state.texture_units[current_bindpoint].target = surface->Target();
            state.texture_units[current_bindpoint].swizzle.r =
                MaxwellToGL::SwizzleSource(texture.tic.x_source);
            state.texture_units[current_bindpoint].swizzle.g =
                MaxwellToGL::SwizzleSource(texture.tic.y_source);
            state.texture_units[current_bindpoint].swizzle.b =
                MaxwellToGL::SwizzleSource(texture.tic.z_source);
            state.texture_units[current_bindpoint].swizzle.a =
                MaxwellToGL::SwizzleSource(texture.tic.w_source);
        } else {
            // Can occur when texture addr is null or its memory is unmapped/invalid
            state.texture_units[current_bindpoint].texture = 0;
        }
    }

    return current_unit + static_cast<u32>(entries.size());
}

void RasterizerOpenGL::SyncViewport(OpenGLState& current_state) {
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;
    const bool geometry_shaders_enabled =
        regs.IsShaderConfigEnabled(static_cast<size_t>(Maxwell::ShaderProgram::Geometry));
    const std::size_t viewport_count =
        geometry_shaders_enabled ? Tegra::Engines::Maxwell3D::Regs::NumViewports : 1;
    for (std::size_t i = 0; i < viewport_count; i++) {
        auto& viewport = current_state.viewports[i];
        const auto& src = regs.viewports[i];
        const MathUtil::Rectangle<s32> viewport_rect{regs.viewport_transform[i].GetRect()};
        viewport.x = viewport_rect.left;
        viewport.y = viewport_rect.bottom;
        viewport.width = viewport_rect.GetWidth();
        viewport.height = viewport_rect.GetHeight();
        viewport.depth_range_far = regs.viewports[i].depth_range_far;
        viewport.depth_range_near = regs.viewports[i].depth_range_near;
    }
}

void RasterizerOpenGL::SyncClipEnabled() {
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;
    state.clip_distance[0] = regs.clip_distance_enabled.c0 != 0;
    state.clip_distance[1] = regs.clip_distance_enabled.c1 != 0;
    state.clip_distance[2] = regs.clip_distance_enabled.c2 != 0;
    state.clip_distance[3] = regs.clip_distance_enabled.c3 != 0;
    state.clip_distance[4] = regs.clip_distance_enabled.c4 != 0;
    state.clip_distance[5] = regs.clip_distance_enabled.c5 != 0;
    state.clip_distance[6] = regs.clip_distance_enabled.c6 != 0;
    state.clip_distance[7] = regs.clip_distance_enabled.c7 != 0;
}

void RasterizerOpenGL::SyncClipCoef() {
    UNREACHABLE();
}

void RasterizerOpenGL::SyncCullMode() {
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;

    state.cull.enabled = regs.cull.enabled != 0;

    if (state.cull.enabled) {
        state.cull.front_face = MaxwellToGL::FrontFace(regs.cull.front_face);
        state.cull.mode = MaxwellToGL::CullFace(regs.cull.cull_face);

        const bool flip_triangles{regs.screen_y_control.triangle_rast_flip == 0 ||
                                  regs.viewport_transform[0].scale_y < 0.0f};

        // If the GPU is configured to flip the rasterized triangles, then we need to flip the
        // notion of front and back. Note: We flip the triangles when the value of the register is 0
        // because OpenGL already does it for us.
        if (flip_triangles) {
            if (state.cull.front_face == GL_CCW)
                state.cull.front_face = GL_CW;
            else if (state.cull.front_face == GL_CW)
                state.cull.front_face = GL_CCW;
        }
    }
}

void RasterizerOpenGL::SyncPrimitiveRestart() {
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;

    state.primitive_restart.enabled = regs.primitive_restart.enabled;
    state.primitive_restart.index = regs.primitive_restart.index;
}

void RasterizerOpenGL::SyncDepthTestState() {
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;

    state.depth.test_enabled = regs.depth_test_enable != 0;
    state.depth.write_mask = regs.depth_write_enabled ? GL_TRUE : GL_FALSE;

    if (!state.depth.test_enabled)
        return;

    state.depth.test_func = MaxwellToGL::ComparisonOp(regs.depth_test_func);
}

void RasterizerOpenGL::SyncStencilTestState() {
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;
    state.stencil.test_enabled = regs.stencil_enable != 0;

    if (!regs.stencil_enable) {
        return;
    }

    state.stencil.front.test_func = MaxwellToGL::ComparisonOp(regs.stencil_front_func_func);
    state.stencil.front.test_ref = regs.stencil_front_func_ref;
    state.stencil.front.test_mask = regs.stencil_front_func_mask;
    state.stencil.front.action_stencil_fail = MaxwellToGL::StencilOp(regs.stencil_front_op_fail);
    state.stencil.front.action_depth_fail = MaxwellToGL::StencilOp(regs.stencil_front_op_zfail);
    state.stencil.front.action_depth_pass = MaxwellToGL::StencilOp(regs.stencil_front_op_zpass);
    state.stencil.front.write_mask = regs.stencil_front_mask;
    if (regs.stencil_two_side_enable) {
        state.stencil.back.test_func = MaxwellToGL::ComparisonOp(regs.stencil_back_func_func);
        state.stencil.back.test_ref = regs.stencil_back_func_ref;
        state.stencil.back.test_mask = regs.stencil_back_func_mask;
        state.stencil.back.action_stencil_fail = MaxwellToGL::StencilOp(regs.stencil_back_op_fail);
        state.stencil.back.action_depth_fail = MaxwellToGL::StencilOp(regs.stencil_back_op_zfail);
        state.stencil.back.action_depth_pass = MaxwellToGL::StencilOp(regs.stencil_back_op_zpass);
        state.stencil.back.write_mask = regs.stencil_back_mask;
    } else {
        state.stencil.back.test_func = GL_ALWAYS;
        state.stencil.back.test_ref = 0;
        state.stencil.back.test_mask = 0xFFFFFFFF;
        state.stencil.back.write_mask = 0xFFFFFFFF;
        state.stencil.back.action_stencil_fail = GL_KEEP;
        state.stencil.back.action_depth_fail = GL_KEEP;
        state.stencil.back.action_depth_pass = GL_KEEP;
    }
}

void RasterizerOpenGL::SyncColorMask() {
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;
    const std::size_t count =
        regs.independent_blend_enable ? Tegra::Engines::Maxwell3D::Regs::NumRenderTargets : 1;
    for (std::size_t i = 0; i < count; i++) {
        const auto& source = regs.color_mask[regs.color_mask_common ? 0 : i];
        auto& dest = state.color_mask[i];
        dest.red_enabled = (source.R == 0) ? GL_FALSE : GL_TRUE;
        dest.green_enabled = (source.G == 0) ? GL_FALSE : GL_TRUE;
        dest.blue_enabled = (source.B == 0) ? GL_FALSE : GL_TRUE;
        dest.alpha_enabled = (source.A == 0) ? GL_FALSE : GL_TRUE;
    }
}

void RasterizerOpenGL::SyncMultiSampleState() {
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;
    state.multisample_control.alpha_to_coverage = regs.multisample_control.alpha_to_coverage != 0;
    state.multisample_control.alpha_to_one = regs.multisample_control.alpha_to_one != 0;
}

void RasterizerOpenGL::SyncFragmentColorClampState() {
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;
    state.fragment_color_clamp.enabled = regs.frag_color_clamp != 0;
}

void RasterizerOpenGL::SyncBlendState() {
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;

    state.blend_color.red = regs.blend_color.r;
    state.blend_color.green = regs.blend_color.g;
    state.blend_color.blue = regs.blend_color.b;
    state.blend_color.alpha = regs.blend_color.a;

    state.independant_blend.enabled = regs.independent_blend_enable;
    if (!state.independant_blend.enabled) {
        auto& blend = state.blend[0];
        const auto& src = regs.blend;
        blend.enabled = src.enable[0] != 0;
        if (blend.enabled) {
            blend.rgb_equation = MaxwellToGL::BlendEquation(src.equation_rgb);
            blend.src_rgb_func = MaxwellToGL::BlendFunc(src.factor_source_rgb);
            blend.dst_rgb_func = MaxwellToGL::BlendFunc(src.factor_dest_rgb);
            blend.a_equation = MaxwellToGL::BlendEquation(src.equation_a);
            blend.src_a_func = MaxwellToGL::BlendFunc(src.factor_source_a);
            blend.dst_a_func = MaxwellToGL::BlendFunc(src.factor_dest_a);
        }
        for (std::size_t i = 1; i < Tegra::Engines::Maxwell3D::Regs::NumRenderTargets; i++) {
            state.blend[i].enabled = false;
        }
        return;
    }

    for (std::size_t i = 0; i < Tegra::Engines::Maxwell3D::Regs::NumRenderTargets; i++) {
        auto& blend = state.blend[i];
        const auto& src = regs.independent_blend[i];
        blend.enabled = regs.blend.enable[i] != 0;
        if (!blend.enabled)
            continue;
        blend.rgb_equation = MaxwellToGL::BlendEquation(src.equation_rgb);
        blend.src_rgb_func = MaxwellToGL::BlendFunc(src.factor_source_rgb);
        blend.dst_rgb_func = MaxwellToGL::BlendFunc(src.factor_dest_rgb);
        blend.a_equation = MaxwellToGL::BlendEquation(src.equation_a);
        blend.src_a_func = MaxwellToGL::BlendFunc(src.factor_source_a);
        blend.dst_a_func = MaxwellToGL::BlendFunc(src.factor_dest_a);
    }
}

void RasterizerOpenGL::SyncLogicOpState() {
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;

    state.logic_op.enabled = regs.logic_op.enable != 0;

    if (!state.logic_op.enabled)
        return;

    ASSERT_MSG(regs.blend.enable[0] == 0,
               "Blending and logic op can't be enabled at the same time.");

    state.logic_op.operation = MaxwellToGL::LogicOp(regs.logic_op.operation);
}

void RasterizerOpenGL::SyncScissorTest(OpenGLState& current_state) {
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;
    const bool geometry_shaders_enabled =
        regs.IsShaderConfigEnabled(static_cast<size_t>(Maxwell::ShaderProgram::Geometry));
    const std::size_t viewport_count =
        geometry_shaders_enabled ? Tegra::Engines::Maxwell3D::Regs::NumViewports : 1;
    for (std::size_t i = 0; i < viewport_count; i++) {
        const auto& src = regs.scissor_test[i];
        auto& dst = current_state.viewports[i].scissor;
        dst.enabled = (src.enable != 0);
        if (dst.enabled == 0) {
            return;
        }
        const u32 width = src.max_x - src.min_x;
        const u32 height = src.max_y - src.min_y;
        dst.x = src.min_x;
        dst.y = src.min_y;
        dst.width = width;
        dst.height = height;
    }
}

void RasterizerOpenGL::SyncTransformFeedback() {
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;

    if (regs.tfb_enabled != 0) {
        LOG_CRITICAL(Render_OpenGL, "Transform feedbacks are not implemented");
        UNREACHABLE();
    }
}

void RasterizerOpenGL::SyncPointState() {
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;
    state.point.size = regs.point_size;
}

void RasterizerOpenGL::SyncPolygonOffset() {
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;
    state.polygon_offset.fill_enable = regs.polygon_offset_fill_enable != 0;
    state.polygon_offset.line_enable = regs.polygon_offset_line_enable != 0;
    state.polygon_offset.point_enable = regs.polygon_offset_point_enable != 0;
    state.polygon_offset.units = regs.polygon_offset_units;
    state.polygon_offset.factor = regs.polygon_offset_factor;
    state.polygon_offset.clamp = regs.polygon_offset_clamp;
}

void RasterizerOpenGL::CheckAlphaTests() {
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;

    if (regs.alpha_test_enabled != 0 && regs.rt_control.count > 1) {
        LOG_CRITICAL(Render_OpenGL, "Alpha Testing is enabled with Multiple Render Targets, "
                                    "this behavior is undefined.");
        UNREACHABLE();
    }
}

} // namespace OpenGL
