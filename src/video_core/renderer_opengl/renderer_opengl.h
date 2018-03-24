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

class EmuWindow;

/// Structure used for storing information about the textures for each 3DS screen
struct TextureInfo {
    OGLTexture resource;
    GLsizei width;
    GLsizei height;
    GLenum gl_format;
    GLenum gl_type;
    Tegra::FramebufferConfig::PixelFormat pixel_format;
};

/// Structure used for storing information about the display target for each 3DS screen
struct ScreenInfo {
    GLuint display_texture;
    MathUtil::Rectangle<float> display_texcoords;
    TextureInfo texture;
};

class RendererOpenGL : public RendererBase {
public:
    RendererOpenGL();
    ~RendererOpenGL() override;

    /// Swap buffers (render frame)
    void SwapBuffers(boost::optional<const Tegra::FramebufferConfig&> framebuffer) override;

    /**
     * Set the emulator window to use for renderer
     * @param window EmuWindow handle to emulator window to use for rendering
     */
    void SetWindow(EmuWindow* window) override;

    /// Initialize the renderer
    bool Init() override;

    /// Shutdown the renderer
    void ShutDown() override;

private:
    void InitOpenGLObjects();
    void ConfigureFramebufferTexture(TextureInfo& texture,
                                     const Tegra::FramebufferConfig& framebuffer);
    void DrawScreens();
    void DrawSingleScreen(const ScreenInfo& screen_info, float x, float y, float w, float h);
    void UpdateFramerate();

    // Loads framebuffer from emulated memory into the display information structure
    void LoadFBToScreenInfo(const Tegra::FramebufferConfig& framebuffer, ScreenInfo& screen_info);
    // Fills active OpenGL texture with the given RGBA color.
    void LoadColorToActiveGLTexture(u8 color_r, u8 color_g, u8 color_b, u8 color_a,
                                    const TextureInfo& texture);

    EmuWindow* render_window; ///< Handle to render window

    OpenGLState state;

    // OpenGL object IDs
    OGLVertexArray vertex_array;
    OGLBuffer vertex_buffer;
    OGLShader shader;

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
};
