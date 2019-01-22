// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <vector>
#include <glad/glad.h>
#include "common/common_types.h"
#include "common/math_util.h"
#include "video_core/renderer_base.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_state.h"

namespace Core {
class System;
}

namespace Core::Frontend {
class EmuWindow;
}

namespace Layout {
struct FramebufferLayout;
}

namespace OpenGL {

/// Structure used for storing information about the textures for the Switch screen
struct TextureInfo {
    OGLTexture resource;
    GLsizei width;
    GLsizei height;
    GLenum gl_format;
    GLenum gl_type;
    Tegra::FramebufferConfig::PixelFormat pixel_format;
};

/// Structure used for storing information about the display target for the Switch screen
struct ScreenInfo {
    GLuint display_texture;
    const MathUtil::Rectangle<float> display_texcoords{0.0f, 0.0f, 1.0f, 1.0f};
    TextureInfo texture;
};

/// Helper class to acquire/release OpenGL context within a given scope
class ScopeAcquireGLContext : NonCopyable {
public:
    explicit ScopeAcquireGLContext(Core::Frontend::EmuWindow& window);
    ~ScopeAcquireGLContext();

private:
    Core::Frontend::EmuWindow& emu_window;
};

class RendererOpenGL : public VideoCore::RendererBase {
public:
    explicit RendererOpenGL(Core::Frontend::EmuWindow& window, Core::System& system);
    ~RendererOpenGL() override;

    /// Swap buffers (render frame)
    void SwapBuffers(
        std::optional<std::reference_wrapper<const Tegra::FramebufferConfig>> framebuffer) override;

    /// Initialize the renderer
    bool Init() override;

    /// Shutdown the renderer
    void ShutDown() override;

private:
    void InitOpenGLObjects();
    void CreateRasterizer();

    void ConfigureFramebufferTexture(TextureInfo& texture,
                                     const Tegra::FramebufferConfig& framebuffer);
    void DrawScreen(const Layout::FramebufferLayout& layout);
    void DrawScreenTriangles(const ScreenInfo& screen_info, float x, float y, float w, float h);
    void UpdateFramerate();

    void CaptureScreenshot();

    // Loads framebuffer from emulated memory into the display information structure
    void LoadFBToScreenInfo(const Tegra::FramebufferConfig& framebuffer);
    // Fills active OpenGL texture with the given RGBA color.
    void LoadColorToActiveGLTexture(u8 color_r, u8 color_g, u8 color_b, u8 color_a,
                                    const TextureInfo& texture);

    Core::System& system;

    OpenGLState state;

    // OpenGL object IDs
    OGLVertexArray vertex_array;
    OGLBuffer vertex_buffer;
    OGLProgram shader;
    OGLFramebuffer screenshot_framebuffer;

    /// Display information for Switch screen
    ScreenInfo screen_info;

    /// OpenGL framebuffer data
    std::vector<u8> gl_framebuffer_data;

    // Shader uniform location indices
    GLuint uniform_modelview_matrix;
    GLuint uniform_color_texture;

    // Shader attribute input indices
    GLuint attrib_position;
    GLuint attrib_tex_coord;

    /// Used for transforming the framebuffer orientation
    Tegra::FramebufferConfig::TransformFlags framebuffer_transform_flags;
    MathUtil::Rectangle<int> framebuffer_crop_rect;
};

} // namespace OpenGL
