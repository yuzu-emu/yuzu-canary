// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
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
using PixelFormat = SurfaceParams::PixelFormat;
using SurfaceType = SurfaceParams::SurfaceType;

MICROPROFILE_DEFINE(OpenGL_VAO, "OpenGL", "Vertex Array Setup", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_Shader, "OpenGL", "Shader Setup", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_UBO, "OpenGL", "Const Buffer Setup", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_Index, "OpenGL", "Index Buffer Setup", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_Texture, "OpenGL", "Texture Setup", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_Framebuffer, "OpenGL", "Framebuffer Setup", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_Drawing, "OpenGL", "Drawing", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_Blits, "OpenGL", "Blits", MP_RGB(128, 128, 192));
MICROPROFILE_DEFINE(OpenGL_CacheManagement, "OpenGL", "Cache Mgmt", MP_RGB(100, 255, 100));

RasterizerOpenGL::RasterizerOpenGL(Core::Frontend::EmuWindow& window, ScreenInfo& info)
    : emu_window{window}, screen_info{info}, buffer_cache(STREAM_BUFFER_SIZE) {
    // Create sampler objects
    for (size_t i = 0; i < texture_samplers.size(); ++i) {
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
        } else if (extension == "GL_ARB_separate_shader_objects") {
            has_ARB_separate_shader_objects = true;
        } else if (extension == "GL_ARB_vertex_attrib_binding") {
            has_ARB_vertex_attrib_binding = true;
        }
    }

    ASSERT_MSG(has_ARB_separate_shader_objects, "has_ARB_separate_shader_objects is unsupported");

    // Clipping plane 0 is always enabled for PICA fixed clip plane z <= 0
    state.clip_distance[0] = true;

    // Create render framebuffer
    framebuffer.Create();

    shader_program_manager = std::make_unique<GLShader::ProgramManager>();
    state.draw.shader_program = 0;
    state.Apply();

    glEnable(GL_BLEND);

    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &uniform_buffer_alignment);

    LOG_CRITICAL(Render_OpenGL, "Sync fixed function OpenGL state here!");
}

RasterizerOpenGL::~RasterizerOpenGL() {}

void RasterizerOpenGL::SetupVertexArrays() {
    MICROPROFILE_SCOPE(OpenGL_VAO);
    const auto& gpu = Core::System::GetInstance().GPU().Maxwell3D();
    const auto& regs = gpu.regs;

    auto [iter, is_cache_miss] = vertex_array_cache.try_emplace(regs.vertex_attrib_format);
    auto& VAO = iter->second;

    if (is_cache_miss) {
        VAO.Create();
        state.draw.vertex_array = VAO.handle;
        state.Apply();

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
    state.draw.vertex_buffer = buffer_cache.GetHandle();
    state.Apply();

    // Upload all guest vertex arrays sequentially to our buffer
    for (u32 index = 0; index < Maxwell::NumVertexArrays; ++index) {
        const auto& vertex_array = regs.vertex_array[index];
        if (!vertex_array.IsEnabled())
            continue;

        Tegra::GPUVAddr start = vertex_array.StartAddress();
        const Tegra::GPUVAddr end = regs.vertex_array_limit[index].LimitAddress();

        if (regs.instanced_arrays.IsInstancingEnabled(index) && vertex_array.divisor != 0) {
            start += vertex_array.stride * (gpu.state.current_instance / vertex_array.divisor);
        }

        ASSERT(end > start);
        u64 size = end - start + 1;

        GLintptr vertex_buffer_offset = buffer_cache.UploadMemory(start, size);

        // Bind the vertex array to the buffer at the current offset.
        glBindVertexBuffer(index, buffer_cache.GetHandle(), vertex_buffer_offset,
                           vertex_array.stride);

        if (regs.instanced_arrays.IsInstancingEnabled(index) && vertex_array.divisor != 0) {
            // Tell OpenGL that this is an instanced vertex buffer to prevent accessing different
            // indexes on each vertex. We do the instance indexing manually by incrementing the
            // start address of the vertex buffer.
            glVertexBindingDivisor(index, 1);
        } else {
            // Disable the vertex buffer instancing.
            glVertexBindingDivisor(index, 0);
        }
    }
}

void RasterizerOpenGL::SetupShaders() {
    MICROPROFILE_SCOPE(OpenGL_Shader);
    auto& gpu = Core::System::GetInstance().GPU().Maxwell3D();

    // Next available bindpoints to use when uploading the const buffers and textures to the GLSL
    // shaders. The constbuffer bindpoint starts after the shader stage configuration bind points.
    u32 current_constbuffer_bindpoint = Tegra::Engines::Maxwell3D::Regs::MaxShaderStage;
    u32 current_texture_bindpoint = 0;

    for (size_t index = 0; index < Maxwell::MaxShaderProgram; ++index) {
        auto& shader_config = gpu.regs.shader_config[index];
        const Maxwell::ShaderProgram program{static_cast<Maxwell::ShaderProgram>(index)};

        // Skip stages that are not enabled
        if (!gpu.regs.IsShaderConfigEnabled(index)) {
            continue;
        }

        const size_t stage{index == 0 ? 0 : index - 1}; // Stage indices are 0 - 5

        GLShader::MaxwellUniformData ubo{};
        ubo.SetFromRegs(gpu.state.shader_stages[stage]);
        GLintptr offset = buffer_cache.UploadHostMemory(
            &ubo, sizeof(ubo), static_cast<size_t>(uniform_buffer_alignment));

        // Bind the buffer
        glBindBufferRange(GL_UNIFORM_BUFFER, stage, buffer_cache.GetHandle(), offset, sizeof(ubo));

        Shader shader{shader_cache.GetStageProgram(program)};

        switch (program) {
        case Maxwell::ShaderProgram::VertexA:
        case Maxwell::ShaderProgram::VertexB: {
            shader_program_manager->UseProgrammableVertexShader(shader->GetProgramHandle());
            break;
        }
        case Maxwell::ShaderProgram::Fragment: {
            shader_program_manager->UseProgrammableFragmentShader(shader->GetProgramHandle());
            break;
        }
        default:
            LOG_CRITICAL(HW_GPU, "Unimplemented shader index={}, enable={}, offset=0x{:08X}", index,
                         shader_config.enable.Value(), shader_config.offset);
            UNREACHABLE();
        }

        // Configure the const buffers for this shader stage.
        current_constbuffer_bindpoint = SetupConstBuffers(static_cast<Maxwell::ShaderStage>(stage),
                                                          shader, current_constbuffer_bindpoint);

        // Configure the textures for this shader stage.
        current_texture_bindpoint = SetupTextures(static_cast<Maxwell::ShaderStage>(stage), shader,
                                                  current_texture_bindpoint);

        // When VertexA is enabled, we have dual vertex shaders
        if (program == Maxwell::ShaderProgram::VertexA) {
            // VertexB was combined with VertexA, so we skip the VertexB iteration
            index++;
        }
    }

    state.Apply();

    shader_program_manager->UseTrivialGeometryShader();
}

size_t RasterizerOpenGL::CalculateVertexArraysSize() const {
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;

    size_t size = 0;
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

std::pair<Surface, Surface> RasterizerOpenGL::ConfigureFramebuffers(bool using_color_fb,
                                                                    bool using_depth_fb,
                                                                    bool preserve_contents) {
    MICROPROFILE_SCOPE(OpenGL_Framebuffer);
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;

    if (regs.rt[0].format == Tegra::RenderTargetFormat::NONE) {
        LOG_ERROR(HW_GPU, "RenderTargetFormat is not configured");
        using_color_fb = false;
    }

    const bool has_stencil = regs.stencil_enable;
    const bool write_color_fb =
        state.color_mask.red_enabled == GL_TRUE || state.color_mask.green_enabled == GL_TRUE ||
        state.color_mask.blue_enabled == GL_TRUE || state.color_mask.alpha_enabled == GL_TRUE;

    const bool write_depth_fb =
        (state.depth.test_enabled && state.depth.write_mask == GL_TRUE) ||
        (has_stencil && (state.stencil.front.write_mask || state.stencil.back.write_mask));

    Surface color_surface;
    Surface depth_surface;
    MathUtil::Rectangle<u32> surfaces_rect;
    std::tie(color_surface, depth_surface, surfaces_rect) =
        res_cache.GetFramebufferSurfaces(using_color_fb, using_depth_fb, preserve_contents);

    const MathUtil::Rectangle<s32> viewport_rect{regs.viewport_transform[0].GetRect()};
    const MathUtil::Rectangle<u32> draw_rect{
        static_cast<u32>(std::clamp<s32>(static_cast<s32>(surfaces_rect.left) + viewport_rect.left,
                                         surfaces_rect.left, surfaces_rect.right)), // Left
        static_cast<u32>(std::clamp<s32>(static_cast<s32>(surfaces_rect.bottom) + viewport_rect.top,
                                         surfaces_rect.bottom, surfaces_rect.top)), // Top
        static_cast<u32>(std::clamp<s32>(static_cast<s32>(surfaces_rect.left) + viewport_rect.right,
                                         surfaces_rect.left, surfaces_rect.right)), // Right
        static_cast<u32>(
            std::clamp<s32>(static_cast<s32>(surfaces_rect.bottom) + viewport_rect.bottom,
                            surfaces_rect.bottom, surfaces_rect.top))}; // Bottom

    // Bind the framebuffer surfaces
    BindFramebufferSurfaces(color_surface, depth_surface, has_stencil);

    SyncViewport(surfaces_rect);

    // Viewport can have negative offsets or larger dimensions than our framebuffer sub-rect. Enable
    // scissor test to prevent drawing outside of the framebuffer region
    state.scissor.enabled = true;
    state.scissor.x = draw_rect.left;
    state.scissor.y = draw_rect.bottom;
    state.scissor.width = draw_rect.GetWidth();
    state.scissor.height = draw_rect.GetHeight();
    state.Apply();

    // Only return the surface to be marked as dirty if writing to it is enabled.
    return std::make_pair(write_color_fb ? color_surface : nullptr,
                          write_depth_fb ? depth_surface : nullptr);
}

void RasterizerOpenGL::Clear() {
    const auto prev_state{state};
    SCOPE_EXIT({ prev_state.Apply(); });

    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;
    bool use_color_fb = false;
    bool use_depth_fb = false;

    OpenGLState clear_state;
    clear_state.draw.draw_framebuffer = state.draw.draw_framebuffer;
    clear_state.color_mask.red_enabled = regs.clear_buffers.R ? GL_TRUE : GL_FALSE;
    clear_state.color_mask.green_enabled = regs.clear_buffers.G ? GL_TRUE : GL_FALSE;
    clear_state.color_mask.blue_enabled = regs.clear_buffers.B ? GL_TRUE : GL_FALSE;
    clear_state.color_mask.alpha_enabled = regs.clear_buffers.A ? GL_TRUE : GL_FALSE;

    GLbitfield clear_mask{};
    if (regs.clear_buffers.R || regs.clear_buffers.G || regs.clear_buffers.B ||
        regs.clear_buffers.A) {
        if (regs.clear_buffers.RT == 0) {
            // We only support clearing the first color attachment for now
            clear_mask |= GL_COLOR_BUFFER_BIT;
            use_color_fb = true;
        } else {
            // TODO(subv): Add support for the other color attachments
            LOG_CRITICAL(HW_GPU, "Clear unimplemented for RT {}", regs.clear_buffers.RT);
        }
    }
    if (regs.clear_buffers.Z) {
        ASSERT_MSG(regs.zeta_enable != 0, "Tried to clear Z but buffer is not enabled!");
        use_depth_fb = true;
        clear_mask |= GL_DEPTH_BUFFER_BIT;

        // Always enable the depth write when clearing the depth buffer. The depth write mask is
        // ignored when clearing the buffer in the Switch, but OpenGL obeys it so we set it to true.
        clear_state.depth.test_enabled = true;
        clear_state.depth.test_func = GL_ALWAYS;
    }
    if (regs.clear_buffers.S) {
        ASSERT_MSG(regs.zeta_enable != 0, "Tried to clear stencil but buffer is not enabled!");
        use_depth_fb = true;
        clear_mask |= GL_STENCIL_BUFFER_BIT;
        clear_state.stencil.test_enabled = true;
    }

    if (!use_color_fb && !use_depth_fb) {
        // No color surface nor depth/stencil surface are enabled
        return;
    }

    if (clear_mask == 0) {
        // No clear mask is enabled
        return;
    }

    ScopeAcquireGLContext acquire_context{emu_window};

    auto [dirty_color_surface, dirty_depth_surface] =
        ConfigureFramebuffers(use_color_fb, use_depth_fb, false);

    clear_state.Apply();

    glClearColor(regs.clear_color[0], regs.clear_color[1], regs.clear_color[2],
                 regs.clear_color[3]);
    glClearDepth(regs.clear_depth);
    glClearStencil(regs.clear_stencil);

    glClear(clear_mask);
}

void RasterizerOpenGL::DrawArrays() {
    if (accelerate_draw == AccelDraw::Disabled)
        return;

    MICROPROFILE_SCOPE(OpenGL_Drawing);
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;

    ScopeAcquireGLContext acquire_context{emu_window};

    auto [dirty_color_surface, dirty_depth_surface] =
        ConfigureFramebuffers(true, regs.zeta.Address() != 0 && regs.zeta_enable != 0, true);

    SyncDepthTestState();
    SyncStencilTestState();
    SyncBlendState();
    SyncLogicOpState();
    SyncCullMode();

    // TODO(bunnei): Sync framebuffer_scale uniform here
    // TODO(bunnei): Sync scissorbox uniform(s) here

    // Draw the vertex batch
    const bool is_indexed = accelerate_draw == AccelDraw::Indexed;
    const u64 index_buffer_size{regs.index_array.count * regs.index_array.FormatSizeInBytes()};

    state.draw.vertex_buffer = buffer_cache.GetHandle();
    state.Apply();

    size_t buffer_size = CalculateVertexArraysSize();

    if (is_indexed) {
        buffer_size = Common::AlignUp<size_t>(buffer_size, 4) + index_buffer_size;
    }

    // Uniform space for the 5 shader stages
    buffer_size =
        Common::AlignUp<size_t>(buffer_size, 4) +
        (sizeof(GLShader::MaxwellUniformData) + uniform_buffer_alignment) * Maxwell::MaxShaderStage;

    // Add space for at least 18 constant buffers
    buffer_size += Maxwell::MaxConstBuffers * (MaxConstbufferSize + uniform_buffer_alignment);

    buffer_cache.Map(buffer_size);

    SetupVertexArrays();

    // If indexed mode, copy the index buffer
    GLintptr index_buffer_offset = 0;
    if (is_indexed) {
        MICROPROFILE_SCOPE(OpenGL_Index);
        index_buffer_offset =
            buffer_cache.UploadMemory(regs.index_array.StartAddress(), index_buffer_size);
    }

    SetupShaders();

    buffer_cache.Unmap();

    shader_program_manager->ApplyTo(state);
    state.Apply();

    const GLenum primitive_mode{MaxwellToGL::PrimitiveTopology(regs.draw.topology)};
    if (is_indexed) {
        const GLint base_vertex{static_cast<GLint>(regs.vb_element_base)};

        // Adjust the index buffer offset so it points to the first desired index.
        index_buffer_offset += regs.index_array.first * regs.index_array.FormatSizeInBytes();

        glDrawElementsBaseVertex(primitive_mode, regs.index_array.count,
                                 MaxwellToGL::IndexFormat(regs.index_array.format),
                                 reinterpret_cast<const void*>(index_buffer_offset), base_vertex);
    } else {
        glDrawArrays(primitive_mode, regs.vertex_buffer.first, regs.vertex_buffer.count);
    }

    // Disable scissor test
    state.scissor.enabled = false;

    accelerate_draw = AccelDraw::Disabled;

    // Unbind textures for potential future use as framebuffer attachments
    for (auto& texture_unit : state.texture_units) {
        texture_unit.Unbind();
    }
    state.Apply();
}

void RasterizerOpenGL::NotifyMaxwellRegisterChanged(u32 method) {}

void RasterizerOpenGL::FlushAll() {
    MICROPROFILE_SCOPE(OpenGL_CacheManagement);
}

void RasterizerOpenGL::FlushRegion(VAddr addr, u64 size) {
    MICROPROFILE_SCOPE(OpenGL_CacheManagement);
}

void RasterizerOpenGL::InvalidateRegion(VAddr addr, u64 size) {
    MICROPROFILE_SCOPE(OpenGL_CacheManagement);
    res_cache.InvalidateRegion(addr, size);
    shader_cache.InvalidateRegion(addr, size);
    buffer_cache.InvalidateRegion(addr, size);
}

void RasterizerOpenGL::FlushAndInvalidateRegion(VAddr addr, u64 size) {
    MICROPROFILE_SCOPE(OpenGL_CacheManagement);
    InvalidateRegion(addr, size);
}

bool RasterizerOpenGL::AccelerateDisplayTransfer(const void* config) {
    MICROPROFILE_SCOPE(OpenGL_Blits);
    UNREACHABLE();
    return true;
}

bool RasterizerOpenGL::AccelerateTextureCopy(const void* config) {
    UNREACHABLE();
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
    const auto& pixel_format{SurfaceParams::PixelFormatFromGPUPixelFormat(config.pixel_format)};
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

    // default is GL_LINEAR_MIPMAP_LINEAR
    glSamplerParameteri(sampler.handle, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    // Other attributes have correct defaults
}

void RasterizerOpenGL::SamplerInfo::SyncWithConfig(const Tegra::Texture::TSCEntry& config) {
    GLuint s = sampler.handle;

    if (mag_filter != config.mag_filter) {
        mag_filter = config.mag_filter;
        glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, MaxwellToGL::TextureFilterMode(mag_filter));
    }
    if (min_filter != config.min_filter) {
        min_filter = config.min_filter;
        glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, MaxwellToGL::TextureFilterMode(min_filter));
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

    if (wrap_u == Tegra::Texture::WrapMode::Border || wrap_v == Tegra::Texture::WrapMode::Border ||
        wrap_p == Tegra::Texture::WrapMode::Border) {
        const GLvec4 new_border_color = {{config.border_color_r, config.border_color_g,
                                          config.border_color_b, config.border_color_a}};
        if (border_color != new_border_color) {
            border_color = new_border_color;
            glSamplerParameterfv(s, GL_TEXTURE_BORDER_COLOR, border_color.data());
        }
    }
}

u32 RasterizerOpenGL::SetupConstBuffers(Maxwell::ShaderStage stage, Shader& shader,
                                        u32 current_bindpoint) {
    MICROPROFILE_SCOPE(OpenGL_UBO);
    const auto& gpu = Core::System::GetInstance().GPU();
    const auto& maxwell3d = gpu.Maxwell3D();
    const auto& shader_stage = maxwell3d.state.shader_stages[static_cast<size_t>(stage)];
    const auto& entries = shader->GetShaderEntries().const_buffer_entries;

    // Upload only the enabled buffers from the 16 constbuffers of each shader stage
    for (u32 bindpoint = 0; bindpoint < entries.size(); ++bindpoint) {
        const auto& used_buffer = entries[bindpoint];
        const auto& buffer = shader_stage.const_buffers[used_buffer.GetIndex()];

        if (!buffer.enabled) {
            continue;
        }

        size_t size = 0;

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
            buffer.address, size, static_cast<size_t>(uniform_buffer_alignment));

        glBindBufferRange(GL_UNIFORM_BUFFER, current_bindpoint + bindpoint,
                          buffer_cache.GetHandle(), const_buffer_offset, size);

        // Now configure the bindpoint of the buffer inside the shader
        glUniformBlockBinding(shader->GetProgramHandle(),
                              shader->GetProgramResourceIndex(used_buffer),
                              current_bindpoint + bindpoint);
    }

    return current_bindpoint + static_cast<u32>(entries.size());
}

u32 RasterizerOpenGL::SetupTextures(Maxwell::ShaderStage stage, Shader& shader, u32 current_unit) {
    MICROPROFILE_SCOPE(OpenGL_Texture);
    const auto& gpu = Core::System::GetInstance().GPU();
    const auto& maxwell3d = gpu.Maxwell3D();
    const auto& entries = shader->GetShaderEntries().texture_samplers;

    ASSERT_MSG(current_unit + entries.size() <= std::size(state.texture_units),
               "Exceeded the number of active textures.");

    for (u32 bindpoint = 0; bindpoint < entries.size(); ++bindpoint) {
        const auto& entry = entries[bindpoint];
        u32 current_bindpoint = current_unit + bindpoint;

        // Bind the uniform to the sampler.

        glProgramUniform1i(shader->GetProgramHandle(), shader->GetUniformLocation(entry),
                           current_bindpoint);

        const auto texture = maxwell3d.GetStageTexture(entry.GetStage(), entry.GetOffset());

        if (!texture.enabled) {
            state.texture_units[current_bindpoint].texture = 0;
            continue;
        }

        texture_samplers[current_bindpoint].SyncWithConfig(texture.tsc);
        Surface surface = res_cache.GetTextureSurface(texture);
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

void RasterizerOpenGL::BindFramebufferSurfaces(const Surface& color_surface,
                                               const Surface& depth_surface, bool has_stencil) {
    state.draw.draw_framebuffer = framebuffer.handle;
    state.Apply();

    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           color_surface != nullptr ? color_surface->Texture().handle : 0, 0);
    if (depth_surface != nullptr) {
        if (has_stencil) {
            // attach both depth and stencil
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                                   depth_surface->Texture().handle, 0);
        } else {
            // attach depth
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                                   depth_surface->Texture().handle, 0);
            // clear stencil attachment
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        }
    } else {
        // clear both depth and stencil attachment
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0,
                               0);
    }
}

void RasterizerOpenGL::SyncViewport(const MathUtil::Rectangle<u32>& surfaces_rect) {
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;
    const MathUtil::Rectangle<s32> viewport_rect{regs.viewport_transform[0].GetRect()};

    state.viewport.x = static_cast<GLint>(surfaces_rect.left) + viewport_rect.left;
    state.viewport.y = static_cast<GLint>(surfaces_rect.bottom) + viewport_rect.bottom;
    state.viewport.width = static_cast<GLsizei>(viewport_rect.GetWidth());
    state.viewport.height = static_cast<GLsizei>(viewport_rect.GetHeight());
}

void RasterizerOpenGL::SyncClipEnabled() {
    UNREACHABLE();
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

void RasterizerOpenGL::SyncDepthScale() {
    UNREACHABLE();
}

void RasterizerOpenGL::SyncDepthOffset() {
    UNREACHABLE();
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

    // TODO(bunnei): Verify behavior when this is not set
    ASSERT(regs.stencil_two_side_enable);

    state.stencil.front.test_func = MaxwellToGL::ComparisonOp(regs.stencil_front_func_func);
    state.stencil.front.test_ref = regs.stencil_front_func_ref;
    state.stencil.front.test_mask = regs.stencil_front_func_mask;
    state.stencil.front.action_stencil_fail = MaxwellToGL::StencilOp(regs.stencil_front_op_fail);
    state.stencil.front.action_depth_fail = MaxwellToGL::StencilOp(regs.stencil_front_op_zfail);
    state.stencil.front.action_depth_pass = MaxwellToGL::StencilOp(regs.stencil_front_op_zpass);
    state.stencil.front.write_mask = regs.stencil_front_mask;

    state.stencil.back.test_func = MaxwellToGL::ComparisonOp(regs.stencil_back_func_func);
    state.stencil.back.test_ref = regs.stencil_back_func_ref;
    state.stencil.back.test_mask = regs.stencil_back_func_mask;
    state.stencil.back.action_stencil_fail = MaxwellToGL::StencilOp(regs.stencil_back_op_fail);
    state.stencil.back.action_depth_fail = MaxwellToGL::StencilOp(regs.stencil_back_op_zfail);
    state.stencil.back.action_depth_pass = MaxwellToGL::StencilOp(regs.stencil_back_op_zpass);
    state.stencil.back.write_mask = regs.stencil_back_mask;
}

void RasterizerOpenGL::SyncBlendState() {
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;

    // TODO(Subv): Support more than just render target 0.
    state.blend.enabled = regs.blend.enable[0] != 0;

    if (!state.blend.enabled)
        return;

    ASSERT_MSG(regs.logic_op.enable == 0,
               "Blending and logic op can't be enabled at the same time.");

    ASSERT_MSG(regs.independent_blend_enable == 1, "Only independent blending is implemented");
    ASSERT_MSG(!regs.independent_blend[0].separate_alpha, "Unimplemented");
    state.blend.rgb_equation = MaxwellToGL::BlendEquation(regs.independent_blend[0].equation_rgb);
    state.blend.src_rgb_func = MaxwellToGL::BlendFunc(regs.independent_blend[0].factor_source_rgb);
    state.blend.dst_rgb_func = MaxwellToGL::BlendFunc(regs.independent_blend[0].factor_dest_rgb);
    state.blend.a_equation = MaxwellToGL::BlendEquation(regs.independent_blend[0].equation_a);
    state.blend.src_a_func = MaxwellToGL::BlendFunc(regs.independent_blend[0].factor_source_a);
    state.blend.dst_a_func = MaxwellToGL::BlendFunc(regs.independent_blend[0].factor_dest_a);
}

void RasterizerOpenGL::SyncLogicOpState() {
    const auto& regs = Core::System::GetInstance().GPU().Maxwell3D().regs;

    // TODO(Subv): Support more than just render target 0.
    state.logic_op.enabled = regs.logic_op.enable != 0;

    if (!state.logic_op.enabled)
        return;

    ASSERT_MSG(regs.blend.enable[0] == 0,
               "Blending and logic op can't be enabled at the same time.");

    state.logic_op.operation = MaxwellToGL::LogicOp(regs.logic_op.operation);
}

} // namespace OpenGL
