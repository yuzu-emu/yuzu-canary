// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <bitset>
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
#include "core/hle/kernel/process.h"
#include "core/settings.h"
#include "video_core/engines/kepler_compute.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/memory_manager.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/renderer_opengl/gl_shader_cache.h"
#include "video_core/renderer_opengl/gl_shader_gen.h"
#include "video_core/renderer_opengl/maxwell_to_gl.h"
#include "video_core/renderer_opengl/renderer_opengl.h"

namespace OpenGL {

using Maxwell = Tegra::Engines::Maxwell3D::Regs;

using VideoCore::Surface::PixelFormat;
using VideoCore::Surface::SurfaceTarget;
using VideoCore::Surface::SurfaceType;

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

static std::size_t GetConstBufferSize(const Tegra::Engines::ConstBufferInfo& buffer,
                                      const GLShader::ConstBufferEntry& entry) {
    if (!entry.IsIndirect()) {
        return entry.GetSize();
    }

    if (buffer.size > Maxwell::MaxConstBufferSize) {
        LOG_WARNING(Render_OpenGL, "Indirect constbuffer size {} exceeds maximum {}", buffer.size,
                    Maxwell::MaxConstBufferSize);
        return Maxwell::MaxConstBufferSize;
    }

    return buffer.size;
}

RasterizerOpenGL::RasterizerOpenGL(Core::System& system, Core::Frontend::EmuWindow& emu_window,
                                   ScreenInfo& info)
    : texture_cache{system, *this, device}, shader_cache{*this, system, emu_window, device},
      system{system}, screen_info{info}, buffer_cache{*this, system, STREAM_BUFFER_SIZE} {
    OpenGLState::ApplyDefaultState();

    shader_program_manager = std::make_unique<GLShader::ProgramManager>();
    state.draw.shader_program = 0;
    state.Apply();
    clear_framebuffer.Create();

    LOG_DEBUG(Render_OpenGL, "Sync fixed function OpenGL state here");
    CheckExtensions();
}

RasterizerOpenGL::~RasterizerOpenGL() {}

void RasterizerOpenGL::CheckExtensions() {
    if (!GLAD_GL_ARB_texture_filter_anisotropic && !GLAD_GL_EXT_texture_filter_anisotropic) {
        LOG_WARNING(
            Render_OpenGL,
            "Anisotropic filter is not supported! This can cause graphical issues in some games.");
    }
}

GLuint RasterizerOpenGL::SetupVertexFormat() {
    auto& gpu = system.GPU().Maxwell3D();
    const auto& regs = gpu.regs;

    if (!gpu.dirty.vertex_attrib_format) {
        return state.draw.vertex_array;
    }
    gpu.dirty.vertex_attrib_format = false;

    MICROPROFILE_SCOPE(OpenGL_VAO);

    auto [iter, is_cache_miss] = vertex_array_cache.try_emplace(regs.vertex_attrib_format);
    auto& vao_entry = iter->second;

    if (is_cache_miss) {
        vao_entry.Create();
        const GLuint vao = vao_entry.handle;

        // Eventhough we are using DSA to create this vertex array, there is a bug on Intel's blob
        // that fails to properly create the vertex array if it's not bound even after creating it
        // with glCreateVertexArrays
        state.draw.vertex_array = vao;
        state.ApplyVertexArrayState();

        // Use the vertex array as-is, assumes that the data is formatted correctly for OpenGL.
        // Enables the first 16 vertex attributes always, as we don't know which ones are actually
        // used until shader time. Note, Tegra technically supports 32, but we're capping this to 16
        // for now to avoid OpenGL errors.
        // TODO(Subv): Analyze the shader to identify which attributes are actually used and don't
        // assume every shader uses them all.
        for (u32 index = 0; index < 16; ++index) {
            const auto& attrib = regs.vertex_attrib_format[index];

            // Ignore invalid attributes.
            if (!attrib.IsValid())
                continue;

            const auto& buffer = regs.vertex_array[attrib.buffer];
            LOG_TRACE(Render_OpenGL,
                      "vertex attrib {}, count={}, size={}, type={}, offset={}, normalize={}",
                      index, attrib.ComponentCount(), attrib.SizeString(), attrib.TypeString(),
                      attrib.offset.Value(), attrib.IsNormalized());

            ASSERT(buffer.IsEnabled());

            glEnableVertexArrayAttrib(vao, index);
            if (attrib.type == Tegra::Engines::Maxwell3D::Regs::VertexAttribute::Type::SignedInt ||
                attrib.type ==
                    Tegra::Engines::Maxwell3D::Regs::VertexAttribute::Type::UnsignedInt) {
                glVertexArrayAttribIFormat(vao, index, attrib.ComponentCount(),
                                           MaxwellToGL::VertexType(attrib), attrib.offset);
            } else {
                glVertexArrayAttribFormat(
                    vao, index, attrib.ComponentCount(), MaxwellToGL::VertexType(attrib),
                    attrib.IsNormalized() ? GL_TRUE : GL_FALSE, attrib.offset);
            }
            glVertexArrayAttribBinding(vao, index, attrib.buffer);
        }
    }

    // Rebinding the VAO invalidates the vertex buffer bindings.
    gpu.dirty.ResetVertexArrays();

    state.draw.vertex_array = vao_entry.handle;
    return vao_entry.handle;
}

void RasterizerOpenGL::SetupVertexBuffer(GLuint vao) {
    auto& gpu = system.GPU().Maxwell3D();
    if (!gpu.dirty.vertex_array_buffers)
        return;
    gpu.dirty.vertex_array_buffers = false;

    const auto& regs = gpu.regs;

    MICROPROFILE_SCOPE(OpenGL_VB);

    // Upload all guest vertex arrays sequentially to our buffer
    for (u32 index = 0; index < Maxwell::NumVertexArrays; ++index) {
        if (!gpu.dirty.vertex_array[index])
            continue;
        gpu.dirty.vertex_array[index] = false;
        gpu.dirty.vertex_instance[index] = false;

        const auto& vertex_array = regs.vertex_array[index];
        if (!vertex_array.IsEnabled())
            continue;

        const GPUVAddr start = vertex_array.StartAddress();
        const GPUVAddr end = regs.vertex_array_limit[index].LimitAddress();

        ASSERT(end > start);
        const u64 size = end - start + 1;
        const auto [vertex_buffer, vertex_buffer_offset] = buffer_cache.UploadMemory(start, size);

        // Bind the vertex array to the buffer at the current offset.
        vertex_array_pushbuffer.SetVertexBuffer(index, vertex_buffer, vertex_buffer_offset,
                                                vertex_array.stride);

        if (regs.instanced_arrays.IsInstancingEnabled(index) && vertex_array.divisor != 0) {
            // Enable vertex buffer instancing with the specified divisor.
            glVertexArrayBindingDivisor(vao, index, vertex_array.divisor);
        } else {
            // Disable the vertex buffer instancing.
            glVertexArrayBindingDivisor(vao, index, 0);
        }
    }
}

void RasterizerOpenGL::SetupVertexInstances(GLuint vao) {
    auto& gpu = system.GPU().Maxwell3D();

    if (!gpu.dirty.vertex_instances)
        return;
    gpu.dirty.vertex_instances = false;

    const auto& regs = gpu.regs;
    // Upload all guest vertex arrays sequentially to our buffer
    for (u32 index = 0; index < Maxwell::NumVertexArrays; ++index) {
        if (!gpu.dirty.vertex_instance[index])
            continue;

        gpu.dirty.vertex_instance[index] = false;

        if (regs.instanced_arrays.IsInstancingEnabled(index) &&
            regs.vertex_array[index].divisor != 0) {
            // Enable vertex buffer instancing with the specified divisor.
            glVertexArrayBindingDivisor(vao, index, regs.vertex_array[index].divisor);
        } else {
            // Disable the vertex buffer instancing.
            glVertexArrayBindingDivisor(vao, index, 0);
        }
    }
}

GLintptr RasterizerOpenGL::SetupIndexBuffer() {
    if (accelerate_draw != AccelDraw::Indexed) {
        return 0;
    }
    MICROPROFILE_SCOPE(OpenGL_Index);
    const auto& regs = system.GPU().Maxwell3D().regs;
    const std::size_t size = CalculateIndexBufferSize();
    const auto [buffer, offset] = buffer_cache.UploadMemory(regs.index_array.IndexStart(), size);
    vertex_array_pushbuffer.SetIndexBuffer(buffer);
    return offset;
}

void RasterizerOpenGL::SetupShaders(GLenum primitive_mode) {
    MICROPROFILE_SCOPE(OpenGL_Shader);
    auto& gpu = system.GPU().Maxwell3D();

    BaseBindings base_bindings;
    std::array<bool, Maxwell::NumClipDistances> clip_distances{};

    for (std::size_t index = 0; index < Maxwell::MaxShaderProgram; ++index) {
        const auto& shader_config = gpu.regs.shader_config[index];
        const Maxwell::ShaderProgram program{static_cast<Maxwell::ShaderProgram>(index)};

        // Skip stages that are not enabled
        if (!gpu.regs.IsShaderConfigEnabled(index)) {
            switch (program) {
            case Maxwell::ShaderProgram::Geometry:
                shader_program_manager->UseTrivialGeometryShader();
                break;
            default:
                break;
            }
            continue;
        }

        const std::size_t stage{index == 0 ? 0 : index - 1}; // Stage indices are 0 - 5

        GLShader::MaxwellUniformData ubo{};
        ubo.SetFromRegs(gpu, stage);
        const auto [buffer, offset] =
            buffer_cache.UploadHostMemory(&ubo, sizeof(ubo), device.GetUniformBufferAlignment());

        // Bind the emulation info buffer
        bind_ubo_pushbuffer.Push(buffer, offset, static_cast<GLsizeiptr>(sizeof(ubo)));

        Shader shader{shader_cache.GetStageProgram(program)};

        const auto stage_enum = static_cast<Maxwell::ShaderStage>(stage);
        SetupDrawConstBuffers(stage_enum, shader);
        SetupDrawGlobalMemory(stage_enum, shader);
        const auto texture_buffer_usage{SetupDrawTextures(stage_enum, shader, base_bindings)};

        const ProgramVariant variant{base_bindings, primitive_mode, texture_buffer_usage};
        const auto [program_handle, next_bindings] = shader->GetProgramHandle(variant);

        switch (program) {
        case Maxwell::ShaderProgram::VertexA:
        case Maxwell::ShaderProgram::VertexB:
            shader_program_manager->UseProgrammableVertexShader(program_handle);
            break;
        case Maxwell::ShaderProgram::Geometry:
            shader_program_manager->UseProgrammableGeometryShader(program_handle);
            break;
        case Maxwell::ShaderProgram::Fragment:
            shader_program_manager->UseProgrammableFragmentShader(program_handle);
            break;
        default:
            UNIMPLEMENTED_MSG("Unimplemented shader index={}, enable={}, offset=0x{:08X}", index,
                              shader_config.enable.Value(), shader_config.offset);
        }

        // Workaround for Intel drivers.
        // When a clip distance is enabled but not set in the shader it crops parts of the screen
        // (sometimes it's half the screen, sometimes three quarters). To avoid this, enable the
        // clip distances only when it's written by a shader stage.
        for (std::size_t i = 0; i < Maxwell::NumClipDistances; ++i) {
            clip_distances[i] = clip_distances[i] || shader->GetShaderEntries().clip_distances[i];
        }

        // When VertexA is enabled, we have dual vertex shaders
        if (program == Maxwell::ShaderProgram::VertexA) {
            // VertexB was combined with VertexA, so we skip the VertexB iteration
            index++;
        }

        base_bindings = next_bindings;
    }

    SyncClipEnabled(clip_distances);

    gpu.dirty.shaders = false;
}

std::size_t RasterizerOpenGL::CalculateVertexArraysSize() const {
    const auto& regs = system.GPU().Maxwell3D().regs;

    std::size_t size = 0;
    for (u32 index = 0; index < Maxwell::NumVertexArrays; ++index) {
        if (!regs.vertex_array[index].IsEnabled())
            continue;

        const GPUVAddr start = regs.vertex_array[index].StartAddress();
        const GPUVAddr end = regs.vertex_array_limit[index].LimitAddress();

        ASSERT(end > start);
        size += end - start + 1;
    }

    return size;
}

std::size_t RasterizerOpenGL::CalculateIndexBufferSize() const {
    const auto& regs = system.GPU().Maxwell3D().regs;

    return static_cast<std::size_t>(regs.index_array.count) *
           static_cast<std::size_t>(regs.index_array.FormatSizeInBytes());
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

void RasterizerOpenGL::LoadDiskResources(const std::atomic_bool& stop_loading,
                                         const VideoCore::DiskResourceLoadCallback& callback) {
    shader_cache.LoadDiskCache(stop_loading, callback);
}

void RasterizerOpenGL::ConfigureFramebuffers() {
    MICROPROFILE_SCOPE(OpenGL_Framebuffer);
    auto& gpu = system.GPU().Maxwell3D();
    if (!gpu.dirty.render_settings) {
        return;
    }
    gpu.dirty.render_settings = false;

    texture_cache.GuardRenderTargets(true);

    View depth_surface = texture_cache.GetDepthBufferSurface(true);

    const auto& regs = gpu.regs;
    state.framebuffer_srgb.enabled = regs.framebuffer_srgb != 0;
    UNIMPLEMENTED_IF(regs.rt_separate_frag_data == 0);

    // Bind the framebuffer surfaces
    FramebufferCacheKey fbkey;
    for (std::size_t index = 0; index < Maxwell::NumRenderTargets; ++index) {
        View color_surface{texture_cache.GetColorBufferSurface(index, true)};

        if (color_surface) {
            // Assume that a surface will be written to if it is used as a framebuffer, even
            // if the shader doesn't actually write to it.
            texture_cache.MarkColorBufferInUse(index);
        }

        fbkey.color_attachments[index] = GL_COLOR_ATTACHMENT0 + regs.rt_control.GetMap(index);
        fbkey.colors[index] = std::move(color_surface);
    }
    fbkey.colors_count = regs.rt_control.count;

    if (depth_surface) {
        // Assume that a surface will be written to if it is used as a framebuffer, even if
        // the shader doesn't actually write to it.
        texture_cache.MarkDepthBufferInUse();

        fbkey.stencil_enable = depth_surface->GetSurfaceParams().type == SurfaceType::DepthStencil;
        fbkey.zeta = std::move(depth_surface);
    }

    texture_cache.GuardRenderTargets(false);

    state.draw.draw_framebuffer = framebuffer_cache.GetFramebuffer(fbkey);
    SyncViewport(state);
}

void RasterizerOpenGL::ConfigureClearFramebuffer(OpenGLState& current_state, bool using_color_fb,
                                                 bool using_depth_fb, bool using_stencil_fb) {
    auto& gpu = system.GPU().Maxwell3D();
    const auto& regs = gpu.regs;

    texture_cache.GuardRenderTargets(true);
    View color_surface{};
    if (using_color_fb) {
        color_surface = texture_cache.GetColorBufferSurface(regs.clear_buffers.RT, false);
    }
    View depth_surface{};
    if (using_depth_fb || using_stencil_fb) {
        depth_surface = texture_cache.GetDepthBufferSurface(false);
    }
    texture_cache.GuardRenderTargets(false);

    current_state.draw.draw_framebuffer = clear_framebuffer.handle;
    current_state.ApplyFramebufferState();

    if (color_surface) {
        color_surface->Attach(GL_COLOR_ATTACHMENT0, GL_DRAW_FRAMEBUFFER);
    } else {
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    }

    if (depth_surface) {
        const auto& params = depth_surface->GetSurfaceParams();
        switch (params.type) {
        case VideoCore::Surface::SurfaceType::Depth:
            depth_surface->Attach(GL_DEPTH_ATTACHMENT, GL_DRAW_FRAMEBUFFER);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
            break;
        case VideoCore::Surface::SurfaceType::DepthStencil:
            depth_surface->Attach(GL_DEPTH_STENCIL_ATTACHMENT, GL_DRAW_FRAMEBUFFER);
            break;
        default:
            UNIMPLEMENTED();
        }
    } else {
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0,
                               0);
    }
}

void RasterizerOpenGL::Clear() {
    const auto& maxwell3d = system.GPU().Maxwell3D();

    if (!maxwell3d.ShouldExecute()) {
        return;
    }

    const auto& regs = maxwell3d.regs;
    bool use_color{};
    bool use_depth{};
    bool use_stencil{};

    OpenGLState prev_state{OpenGLState::GetCurState()};
    SCOPE_EXIT({
        prev_state.AllDirty();
        prev_state.Apply();
    });

    OpenGLState clear_state{OpenGLState::GetCurState()};
    clear_state.SetDefaultViewports();
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
        clear_state.depth.write_mask = GL_TRUE;
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

    ConfigureClearFramebuffer(clear_state, use_color, use_depth, use_stencil);

    SyncViewport(clear_state);
    if (regs.clear_flags.scissor) {
        SyncScissorTest(clear_state);
    }

    if (regs.clear_flags.viewport) {
        clear_state.EmulateViewportWithScissor();
    }

    clear_state.AllDirty();
    clear_state.Apply();

    if (use_color) {
        glClearBufferfv(GL_COLOR, 0, regs.clear_color);
    }

    if (use_depth && use_stencil) {
        glClearBufferfi(GL_DEPTH_STENCIL, 0, regs.clear_depth, regs.clear_stencil);
    } else if (use_depth) {
        glClearBufferfv(GL_DEPTH, 0, &regs.clear_depth);
    } else if (use_stencil) {
        glClearBufferiv(GL_STENCIL, 0, &regs.clear_stencil);
    }
}

void RasterizerOpenGL::DrawPrelude() {
    auto& gpu = system.GPU().Maxwell3D();

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
    SyncTransformFeedback();
    SyncPointState();
    SyncPolygonOffset();
    SyncAlphaTest();

    // Draw the vertex batch
    const bool is_indexed = accelerate_draw == AccelDraw::Indexed;

    std::size_t buffer_size = CalculateVertexArraysSize();

    // Add space for index buffer
    if (is_indexed) {
        buffer_size = Common::AlignUp(buffer_size, 4) + CalculateIndexBufferSize();
    }

    // Uniform space for the 5 shader stages
    buffer_size = Common::AlignUp<std::size_t>(buffer_size, 4) +
                  (sizeof(GLShader::MaxwellUniformData) + device.GetUniformBufferAlignment()) *
                      Maxwell::MaxShaderStage;

    // Add space for at least 18 constant buffers
    buffer_size += Maxwell::MaxConstBuffers *
                   (Maxwell::MaxConstBufferSize + device.GetUniformBufferAlignment());

    // Prepare the vertex array.
    buffer_cache.Map(buffer_size);

    // Prepare vertex array format.
    const GLuint vao = SetupVertexFormat();
    vertex_array_pushbuffer.Setup(vao);

    // Upload vertex and index data.
    SetupVertexBuffer(vao);
    SetupVertexInstances(vao);
    index_buffer_offset = SetupIndexBuffer();

    // Prepare packed bindings.
    bind_ubo_pushbuffer.Setup(0);
    bind_ssbo_pushbuffer.Setup(0);

    // Setup shaders and their used resources.
    texture_cache.GuardSamplers(true);
    const auto primitive_mode = MaxwellToGL::PrimitiveTopology(gpu.regs.draw.topology);
    SetupShaders(primitive_mode);
    texture_cache.GuardSamplers(false);

    ConfigureFramebuffers();

    // Signal the buffer cache that we are not going to upload more things.
    const bool invalidate = buffer_cache.Unmap();

    // Now that we are no longer uploading data, we can safely bind the buffers to OpenGL.
    vertex_array_pushbuffer.Bind();
    bind_ubo_pushbuffer.Bind();
    bind_ssbo_pushbuffer.Bind();

    if (invalidate) {
        // As all cached buffers are invalidated, we need to recheck their state.
        gpu.dirty.ResetVertexArrays();
    }

    shader_program_manager->ApplyTo(state);
    state.Apply();

    if (texture_cache.TextureBarrier()) {
        glTextureBarrier();
    }
}

struct DrawParams {
    bool is_indexed{};
    bool is_instanced{};
    GLenum primitive_mode{};
    GLint count{};
    GLint base_vertex{};

    // Indexed settings
    GLenum index_format{};
    GLintptr index_buffer_offset{};

    // Instanced setting
    GLint num_instances{};
    GLint base_instance{};

    void DispatchDraw() {
        if (is_indexed) {
            const auto index_buffer_ptr = reinterpret_cast<const void*>(index_buffer_offset);
            if (is_instanced) {
                glDrawElementsInstancedBaseVertexBaseInstance(primitive_mode, count, index_format,
                                                              index_buffer_ptr, num_instances,
                                                              base_vertex, base_instance);
            } else {
                glDrawElementsBaseVertex(primitive_mode, count, index_format, index_buffer_ptr,
                                         base_vertex);
            }
        } else {
            if (is_instanced) {
                glDrawArraysInstancedBaseInstance(primitive_mode, base_vertex, count, num_instances,
                                                  base_instance);
            } else {
                glDrawArrays(primitive_mode, base_vertex, count);
            }
        }
    }
};

bool RasterizerOpenGL::DrawBatch(bool is_indexed) {
    accelerate_draw = is_indexed ? AccelDraw::Indexed : AccelDraw::Arrays;

    MICROPROFILE_SCOPE(OpenGL_Drawing);

    DrawPrelude();

    auto& maxwell3d = system.GPU().Maxwell3D();
    const auto& regs = maxwell3d.regs;
    const auto current_instance = maxwell3d.state.current_instance;
    DrawParams draw_call{};
    draw_call.is_indexed = is_indexed;
    draw_call.num_instances = static_cast<GLint>(1);
    draw_call.base_instance = static_cast<GLint>(current_instance);
    draw_call.is_instanced = current_instance > 0;
    draw_call.primitive_mode = MaxwellToGL::PrimitiveTopology(regs.draw.topology);
    if (draw_call.is_indexed) {
        draw_call.count = static_cast<GLint>(regs.index_array.count);
        draw_call.base_vertex = static_cast<GLint>(regs.vb_element_base);
        draw_call.index_format = MaxwellToGL::IndexFormat(regs.index_array.format);
        draw_call.index_buffer_offset = index_buffer_offset;
    } else {
        draw_call.count = static_cast<GLint>(regs.vertex_buffer.count);
        draw_call.base_vertex = static_cast<GLint>(regs.vertex_buffer.first);
    }
    draw_call.DispatchDraw();

    maxwell3d.dirty.memory_general = false;
    accelerate_draw = AccelDraw::Disabled;
    return true;
}

bool RasterizerOpenGL::DrawMultiBatch(bool is_indexed) {
    accelerate_draw = is_indexed ? AccelDraw::Indexed : AccelDraw::Arrays;

    MICROPROFILE_SCOPE(OpenGL_Drawing);

    DrawPrelude();

    auto& maxwell3d = system.GPU().Maxwell3D();
    const auto& regs = maxwell3d.regs;
    const auto& draw_setup = maxwell3d.mme_draw;
    DrawParams draw_call{};
    draw_call.is_indexed = is_indexed;
    draw_call.num_instances = static_cast<GLint>(draw_setup.instance_count);
    draw_call.base_instance = static_cast<GLint>(regs.vb_base_instance);
    draw_call.is_instanced = draw_setup.instance_count > 1;
    draw_call.primitive_mode = MaxwellToGL::PrimitiveTopology(regs.draw.topology);
    if (draw_call.is_indexed) {
        draw_call.count = static_cast<GLint>(regs.index_array.count);
        draw_call.base_vertex = static_cast<GLint>(regs.vb_element_base);
        draw_call.index_format = MaxwellToGL::IndexFormat(regs.index_array.format);
        draw_call.index_buffer_offset = index_buffer_offset;
    } else {
        draw_call.count = static_cast<GLint>(regs.vertex_buffer.count);
        draw_call.base_vertex = static_cast<GLint>(regs.vertex_buffer.first);
    }
    draw_call.DispatchDraw();

    maxwell3d.dirty.memory_general = false;
    accelerate_draw = AccelDraw::Disabled;
    return true;
}

void RasterizerOpenGL::DispatchCompute(GPUVAddr code_addr) {
    if (!GLAD_GL_ARB_compute_variable_group_size) {
        LOG_ERROR(Render_OpenGL, "Compute is currently not supported on this device due to the "
                                 "lack of GL_ARB_compute_variable_group_size");
        return;
    }

    auto kernel = shader_cache.GetComputeKernel(code_addr);
    ProgramVariant variant;
    variant.texture_buffer_usage = SetupComputeTextures(kernel);
    SetupComputeImages(kernel);

    const auto [program, next_bindings] = kernel->GetProgramHandle(variant);
    state.draw.shader_program = program;
    state.draw.program_pipeline = 0;

    const std::size_t buffer_size =
        Tegra::Engines::KeplerCompute::NumConstBuffers *
        (Maxwell::MaxConstBufferSize + device.GetUniformBufferAlignment());
    buffer_cache.Map(buffer_size);

    bind_ubo_pushbuffer.Setup(0);
    bind_ssbo_pushbuffer.Setup(0);

    SetupComputeConstBuffers(kernel);
    SetupComputeGlobalMemory(kernel);

    buffer_cache.Unmap();

    bind_ubo_pushbuffer.Bind();
    bind_ssbo_pushbuffer.Bind();

    state.ApplyTextures();
    state.ApplyImages();
    state.ApplyShaderProgram();
    state.ApplyProgramPipeline();

    const auto& launch_desc = system.GPU().KeplerCompute().launch_description;
    glDispatchComputeGroupSizeARB(launch_desc.grid_dim_x, launch_desc.grid_dim_y,
                                  launch_desc.grid_dim_z, launch_desc.block_dim_x,
                                  launch_desc.block_dim_y, launch_desc.block_dim_z);
}

void RasterizerOpenGL::FlushAll() {}

void RasterizerOpenGL::FlushRegion(CacheAddr addr, u64 size) {
    MICROPROFILE_SCOPE(OpenGL_CacheManagement);
    if (!addr || !size) {
        return;
    }
    texture_cache.FlushRegion(addr, size);
    buffer_cache.FlushRegion(addr, size);
}

void RasterizerOpenGL::InvalidateRegion(CacheAddr addr, u64 size) {
    MICROPROFILE_SCOPE(OpenGL_CacheManagement);
    if (!addr || !size) {
        return;
    }
    texture_cache.InvalidateRegion(addr, size);
    shader_cache.InvalidateRegion(addr, size);
    buffer_cache.InvalidateRegion(addr, size);
}

void RasterizerOpenGL::FlushAndInvalidateRegion(CacheAddr addr, u64 size) {
    if (Settings::values.use_accurate_gpu_emulation) {
        FlushRegion(addr, size);
    }
    InvalidateRegion(addr, size);
}

void RasterizerOpenGL::FlushCommands() {
    glFlush();
}

void RasterizerOpenGL::TickFrame() {
    buffer_cache.TickFrame();
}

bool RasterizerOpenGL::AccelerateSurfaceCopy(const Tegra::Engines::Fermi2D::Regs::Surface& src,
                                             const Tegra::Engines::Fermi2D::Regs::Surface& dst,
                                             const Tegra::Engines::Fermi2D::Config& copy_config) {
    MICROPROFILE_SCOPE(OpenGL_Blits);
    texture_cache.DoFermiCopy(src, dst, copy_config);
    return true;
}

bool RasterizerOpenGL::AccelerateDisplay(const Tegra::FramebufferConfig& config,
                                         VAddr framebuffer_addr, u32 pixel_stride) {
    if (!framebuffer_addr) {
        return {};
    }

    MICROPROFILE_SCOPE(OpenGL_CacheManagement);

    const auto surface{
        texture_cache.TryFindFramebufferSurface(Memory::GetPointer(framebuffer_addr))};
    if (!surface) {
        return {};
    }

    // Verify that the cached surface is the same size and format as the requested framebuffer
    const auto& params{surface->GetSurfaceParams()};
    const auto& pixel_format{
        VideoCore::Surface::PixelFormatFromGPUPixelFormat(config.pixel_format)};
    ASSERT_MSG(params.width == config.width, "Framebuffer width is different");
    ASSERT_MSG(params.height == config.height, "Framebuffer height is different");

    if (params.pixel_format != pixel_format) {
        LOG_WARNING(Render_OpenGL, "Framebuffer pixel_format is different");
    }

    screen_info.display_texture = surface->GetTexture();
    screen_info.display_srgb = surface->GetSurfaceParams().srgb_conversion;

    return true;
}

void RasterizerOpenGL::SetupDrawConstBuffers(Tegra::Engines::Maxwell3D::Regs::ShaderStage stage,
                                             const Shader& shader) {
    MICROPROFILE_SCOPE(OpenGL_UBO);
    const auto& stages = system.GPU().Maxwell3D().state.shader_stages;
    const auto& shader_stage = stages[static_cast<std::size_t>(stage)];
    for (const auto& entry : shader->GetShaderEntries().const_buffers) {
        const auto& buffer = shader_stage.const_buffers[entry.GetIndex()];
        SetupConstBuffer(buffer, entry);
    }
}

void RasterizerOpenGL::SetupComputeConstBuffers(const Shader& kernel) {
    MICROPROFILE_SCOPE(OpenGL_UBO);
    const auto& launch_desc = system.GPU().KeplerCompute().launch_description;
    for (const auto& entry : kernel->GetShaderEntries().const_buffers) {
        const auto& config = launch_desc.const_buffer_config[entry.GetIndex()];
        const std::bitset<8> mask = launch_desc.const_buffer_enable_mask.Value();
        Tegra::Engines::ConstBufferInfo buffer;
        buffer.address = config.Address();
        buffer.size = config.size;
        buffer.enabled = mask[entry.GetIndex()];
        SetupConstBuffer(buffer, entry);
    }
}

void RasterizerOpenGL::SetupConstBuffer(const Tegra::Engines::ConstBufferInfo& buffer,
                                        const GLShader::ConstBufferEntry& entry) {
    if (!buffer.enabled) {
        // Set values to zero to unbind buffers
        bind_ubo_pushbuffer.Push(buffer_cache.GetEmptyBuffer(sizeof(float)), 0, sizeof(float));
        return;
    }

    // Align the actual size so it ends up being a multiple of vec4 to meet the OpenGL std140
    // UBO alignment requirements.
    const std::size_t size = Common::AlignUp(GetConstBufferSize(buffer, entry), sizeof(GLvec4));

    const auto alignment = device.GetUniformBufferAlignment();
    const auto [cbuf, offset] = buffer_cache.UploadMemory(buffer.address, size, alignment);
    bind_ubo_pushbuffer.Push(cbuf, offset, size);
}

void RasterizerOpenGL::SetupDrawGlobalMemory(Tegra::Engines::Maxwell3D::Regs::ShaderStage stage,
                                             const Shader& shader) {
    auto& gpu{system.GPU()};
    auto& memory_manager{gpu.MemoryManager()};
    const auto cbufs{gpu.Maxwell3D().state.shader_stages[static_cast<std::size_t>(stage)]};
    for (const auto& entry : shader->GetShaderEntries().global_memory_entries) {
        const auto addr{cbufs.const_buffers[entry.GetCbufIndex()].address + entry.GetCbufOffset()};
        const auto gpu_addr{memory_manager.Read<u64>(addr)};
        const auto size{memory_manager.Read<u32>(addr + 8)};
        SetupGlobalMemory(entry, gpu_addr, size);
    }
}

void RasterizerOpenGL::SetupComputeGlobalMemory(const Shader& kernel) {
    auto& gpu{system.GPU()};
    auto& memory_manager{gpu.MemoryManager()};
    const auto cbufs{gpu.KeplerCompute().launch_description.const_buffer_config};
    for (const auto& entry : kernel->GetShaderEntries().global_memory_entries) {
        const auto addr{cbufs[entry.GetCbufIndex()].Address() + entry.GetCbufOffset()};
        const auto gpu_addr{memory_manager.Read<u64>(addr)};
        const auto size{memory_manager.Read<u32>(addr + 8)};
        SetupGlobalMemory(entry, gpu_addr, size);
    }
}

void RasterizerOpenGL::SetupGlobalMemory(const GLShader::GlobalMemoryEntry& entry,
                                         GPUVAddr gpu_addr, std::size_t size) {
    const auto alignment{device.GetShaderStorageBufferAlignment()};
    const auto [ssbo, buffer_offset] =
        buffer_cache.UploadMemory(gpu_addr, size, alignment, entry.IsWritten());
    bind_ssbo_pushbuffer.Push(ssbo, buffer_offset, static_cast<GLsizeiptr>(size));
}

TextureBufferUsage RasterizerOpenGL::SetupDrawTextures(Maxwell::ShaderStage stage,
                                                       const Shader& shader,
                                                       BaseBindings base_bindings) {
    MICROPROFILE_SCOPE(OpenGL_Texture);
    const auto& gpu = system.GPU();
    const auto& maxwell3d = gpu.Maxwell3D();
    const auto& entries = shader->GetShaderEntries().samplers;

    ASSERT_MSG(base_bindings.sampler + entries.size() <= std::size(state.textures),
               "Exceeded the number of active textures.");

    TextureBufferUsage texture_buffer_usage{0};

    for (u32 bindpoint = 0; bindpoint < entries.size(); ++bindpoint) {
        const auto& entry = entries[bindpoint];
        const auto texture = [&]() {
            if (!entry.IsBindless()) {
                return maxwell3d.GetStageTexture(stage, entry.GetOffset());
            }
            const auto cbuf = entry.GetBindlessCBuf();
            Tegra::Texture::TextureHandle tex_handle;
            tex_handle.raw = maxwell3d.AccessConstBuffer32(stage, cbuf.first, cbuf.second);
            return maxwell3d.GetTextureInfo(tex_handle, entry.GetOffset());
        }();

        if (SetupTexture(base_bindings.sampler + bindpoint, texture, entry)) {
            texture_buffer_usage.set(bindpoint);
        }
    }

    return texture_buffer_usage;
}

TextureBufferUsage RasterizerOpenGL::SetupComputeTextures(const Shader& kernel) {
    MICROPROFILE_SCOPE(OpenGL_Texture);
    const auto& compute = system.GPU().KeplerCompute();
    const auto& entries = kernel->GetShaderEntries().samplers;

    ASSERT_MSG(entries.size() <= std::size(state.textures),
               "Exceeded the number of active textures.");

    TextureBufferUsage texture_buffer_usage{0};

    for (u32 bindpoint = 0; bindpoint < entries.size(); ++bindpoint) {
        const auto& entry = entries[bindpoint];
        const auto texture = [&]() {
            if (!entry.IsBindless()) {
                return compute.GetTexture(entry.GetOffset());
            }
            const auto cbuf = entry.GetBindlessCBuf();
            Tegra::Texture::TextureHandle tex_handle;
            tex_handle.raw = compute.AccessConstBuffer32(cbuf.first, cbuf.second);
            return compute.GetTextureInfo(tex_handle, entry.GetOffset());
        }();

        if (SetupTexture(bindpoint, texture, entry)) {
            texture_buffer_usage.set(bindpoint);
        }
    }

    return texture_buffer_usage;
}

bool RasterizerOpenGL::SetupTexture(u32 binding, const Tegra::Texture::FullTextureInfo& texture,
                                    const GLShader::SamplerEntry& entry) {
    state.samplers[binding] = sampler_cache.GetSampler(texture.tsc);

    const auto view = texture_cache.GetTextureSurface(texture.tic, entry);
    if (!view) {
        // Can occur when texture addr is null or its memory is unmapped/invalid
        state.textures[binding] = 0;
        return false;
    }
    state.textures[binding] = view->GetTexture();

    if (view->GetSurfaceParams().IsBuffer()) {
        return true;
    }

    // Apply swizzle to textures that are not buffers.
    view->ApplySwizzle(texture.tic.x_source, texture.tic.y_source, texture.tic.z_source,
                       texture.tic.w_source);
    return false;
}

void RasterizerOpenGL::SetupComputeImages(const Shader& shader) {
    const auto& compute = system.GPU().KeplerCompute();
    const auto& entries = shader->GetShaderEntries().images;
    for (u32 bindpoint = 0; bindpoint < entries.size(); ++bindpoint) {
        const auto& entry = entries[bindpoint];
        const auto tic = [&]() {
            if (!entry.IsBindless()) {
                return compute.GetTexture(entry.GetOffset()).tic;
            }
            const auto cbuf = entry.GetBindlessCBuf();
            Tegra::Texture::TextureHandle tex_handle;
            tex_handle.raw = compute.AccessConstBuffer32(cbuf.first, cbuf.second);
            return compute.GetTextureInfo(tex_handle, entry.GetOffset()).tic;
        }();
        SetupImage(bindpoint, tic, entry);
    }
}

void RasterizerOpenGL::SetupImage(u32 binding, const Tegra::Texture::TICEntry& tic,
                                  const GLShader::ImageEntry& entry) {
    const auto view = texture_cache.GetImageSurface(tic, entry);
    if (!view) {
        state.images[binding] = 0;
        return;
    }
    if (!tic.IsBuffer()) {
        view->ApplySwizzle(tic.x_source, tic.y_source, tic.z_source, tic.w_source);
    }
    if (entry.IsWritten()) {
        view->MarkAsModified(texture_cache.Tick());
    }
    state.images[binding] = view->GetTexture();
}

void RasterizerOpenGL::SyncViewport(OpenGLState& current_state) {
    const auto& regs = system.GPU().Maxwell3D().regs;
    const bool geometry_shaders_enabled =
        regs.IsShaderConfigEnabled(static_cast<size_t>(Maxwell::ShaderProgram::Geometry));
    const std::size_t viewport_count =
        geometry_shaders_enabled ? Tegra::Engines::Maxwell3D::Regs::NumViewports : 1;
    for (std::size_t i = 0; i < viewport_count; i++) {
        auto& viewport = current_state.viewports[i];
        const auto& src = regs.viewports[i];
        const Common::Rectangle<s32> viewport_rect{regs.viewport_transform[i].GetRect()};
        viewport.x = viewport_rect.left;
        viewport.y = viewport_rect.bottom;
        viewport.width = viewport_rect.GetWidth();
        viewport.height = viewport_rect.GetHeight();
        viewport.depth_range_far = src.depth_range_far;
        viewport.depth_range_near = src.depth_range_near;
    }
    state.depth_clamp.far_plane = regs.view_volume_clip_control.depth_clamp_far != 0;
    state.depth_clamp.near_plane = regs.view_volume_clip_control.depth_clamp_near != 0;
}

void RasterizerOpenGL::SyncClipEnabled(
    const std::array<bool, Maxwell::Regs::NumClipDistances>& clip_mask) {

    const auto& regs = system.GPU().Maxwell3D().regs;
    const std::array<bool, Maxwell::Regs::NumClipDistances> reg_state{
        regs.clip_distance_enabled.c0 != 0, regs.clip_distance_enabled.c1 != 0,
        regs.clip_distance_enabled.c2 != 0, regs.clip_distance_enabled.c3 != 0,
        regs.clip_distance_enabled.c4 != 0, regs.clip_distance_enabled.c5 != 0,
        regs.clip_distance_enabled.c6 != 0, regs.clip_distance_enabled.c7 != 0};

    for (std::size_t i = 0; i < Maxwell::Regs::NumClipDistances; ++i) {
        state.clip_distance[i] = reg_state[i] && clip_mask[i];
    }
}

void RasterizerOpenGL::SyncClipCoef() {
    UNIMPLEMENTED();
}

void RasterizerOpenGL::SyncCullMode() {
    auto& maxwell3d = system.GPU().Maxwell3D();

    const auto& regs = maxwell3d.regs;

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
    const auto& regs = system.GPU().Maxwell3D().regs;

    state.primitive_restart.enabled = regs.primitive_restart.enabled;
    state.primitive_restart.index = regs.primitive_restart.index;
}

void RasterizerOpenGL::SyncDepthTestState() {
    const auto& regs = system.GPU().Maxwell3D().regs;

    state.depth.test_enabled = regs.depth_test_enable != 0;
    state.depth.write_mask = regs.depth_write_enabled ? GL_TRUE : GL_FALSE;

    if (!state.depth.test_enabled) {
        return;
    }

    state.depth.test_func = MaxwellToGL::ComparisonOp(regs.depth_test_func);
}

void RasterizerOpenGL::SyncStencilTestState() {
    auto& maxwell3d = system.GPU().Maxwell3D();
    if (!maxwell3d.dirty.stencil_test) {
        return;
    }
    maxwell3d.dirty.stencil_test = false;

    const auto& regs = maxwell3d.regs;
    state.stencil.test_enabled = regs.stencil_enable != 0;
    state.MarkDirtyStencilState();

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
    auto& maxwell3d = system.GPU().Maxwell3D();
    if (!maxwell3d.dirty.color_mask) {
        return;
    }
    const auto& regs = maxwell3d.regs;

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

    state.MarkDirtyColorMask();
    maxwell3d.dirty.color_mask = false;
}

void RasterizerOpenGL::SyncMultiSampleState() {
    const auto& regs = system.GPU().Maxwell3D().regs;
    state.multisample_control.alpha_to_coverage = regs.multisample_control.alpha_to_coverage != 0;
    state.multisample_control.alpha_to_one = regs.multisample_control.alpha_to_one != 0;
}

void RasterizerOpenGL::SyncFragmentColorClampState() {
    const auto& regs = system.GPU().Maxwell3D().regs;
    state.fragment_color_clamp.enabled = regs.frag_color_clamp != 0;
}

void RasterizerOpenGL::SyncBlendState() {
    auto& maxwell3d = system.GPU().Maxwell3D();
    if (!maxwell3d.dirty.blend_state) {
        return;
    }
    const auto& regs = maxwell3d.regs;

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
        maxwell3d.dirty.blend_state = false;
        state.MarkDirtyBlendState();
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

    state.MarkDirtyBlendState();
    maxwell3d.dirty.blend_state = false;
}

void RasterizerOpenGL::SyncLogicOpState() {
    const auto& regs = system.GPU().Maxwell3D().regs;

    state.logic_op.enabled = regs.logic_op.enable != 0;

    if (!state.logic_op.enabled)
        return;

    ASSERT_MSG(regs.blend.enable[0] == 0,
               "Blending and logic op can't be enabled at the same time.");

    state.logic_op.operation = MaxwellToGL::LogicOp(regs.logic_op.operation);
}

void RasterizerOpenGL::SyncScissorTest(OpenGLState& current_state) {
    const auto& regs = system.GPU().Maxwell3D().regs;
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
    const auto& regs = system.GPU().Maxwell3D().regs;
    UNIMPLEMENTED_IF_MSG(regs.tfb_enabled != 0, "Transform feedbacks are not implemented");
}

void RasterizerOpenGL::SyncPointState() {
    const auto& regs = system.GPU().Maxwell3D().regs;
    // Limit the point size to 1 since nouveau sometimes sets a point size of 0 (and that's invalid
    // in OpenGL).
    state.point.size = std::max(1.0f, regs.point_size);
}

void RasterizerOpenGL::SyncPolygonOffset() {
    auto& maxwell3d = system.GPU().Maxwell3D();
    if (!maxwell3d.dirty.polygon_offset) {
        return;
    }
    const auto& regs = maxwell3d.regs;

    state.polygon_offset.fill_enable = regs.polygon_offset_fill_enable != 0;
    state.polygon_offset.line_enable = regs.polygon_offset_line_enable != 0;
    state.polygon_offset.point_enable = regs.polygon_offset_point_enable != 0;

    // Hardware divides polygon offset units by two
    state.polygon_offset.units = regs.polygon_offset_units / 2.0f;
    state.polygon_offset.factor = regs.polygon_offset_factor;
    state.polygon_offset.clamp = regs.polygon_offset_clamp;

    state.MarkDirtyPolygonOffset();
    maxwell3d.dirty.polygon_offset = false;
}

void RasterizerOpenGL::SyncAlphaTest() {
    const auto& regs = system.GPU().Maxwell3D().regs;
    UNIMPLEMENTED_IF_MSG(regs.alpha_test_enabled != 0 && regs.rt_control.count > 1,
                         "Alpha Testing is enabled with more than one rendertarget");

    state.alpha_test.enabled = regs.alpha_test_enabled;
    if (!state.alpha_test.enabled) {
        return;
    }
    state.alpha_test.func = MaxwellToGL::ComparisonOp(regs.alpha_test_func);
    state.alpha_test.ref = regs.alpha_test_ref;
}

} // namespace OpenGL
