// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cstdlib>
#include <string>
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <fmt/format.h>
#include <glad/glad.h>
#include "common/logging/log.h"
#include "common/scm_rev.h"
#include "core/settings.h"
#include "input_common/main.h"
#include "yuzu_tester/emu_window/emu_window_sdl2_hide.h"

bool EmuWindow_SDL2_Hide::SupportsRequiredGLExtensions() {
    std::vector<std::string> unsupported_ext;

    if (!GLAD_GL_ARB_direct_state_access)
        unsupported_ext.push_back("ARB_direct_state_access");
    if (!GLAD_GL_ARB_vertex_type_10f_11f_11f_rev)
        unsupported_ext.push_back("ARB_vertex_type_10f_11f_11f_rev");
    if (!GLAD_GL_ARB_texture_mirror_clamp_to_edge)
        unsupported_ext.push_back("ARB_texture_mirror_clamp_to_edge");
    if (!GLAD_GL_ARB_multi_bind)
        unsupported_ext.push_back("ARB_multi_bind");

    // Extensions required to support some texture formats.
    if (!GLAD_GL_EXT_texture_compression_s3tc)
        unsupported_ext.push_back("EXT_texture_compression_s3tc");
    if (!GLAD_GL_ARB_texture_compression_rgtc)
        unsupported_ext.push_back("ARB_texture_compression_rgtc");
    if (!GLAD_GL_ARB_depth_buffer_float)
        unsupported_ext.push_back("ARB_depth_buffer_float");

    for (const std::string& ext : unsupported_ext)
        LOG_CRITICAL(Frontend, "Unsupported GL extension: {}", ext);

    return unsupported_ext.empty();
}

EmuWindow_SDL2_Hide::EmuWindow_SDL2_Hide() {
    // Initialize the window
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        LOG_CRITICAL(Frontend, "Failed to initialize SDL2! Exiting...");
        exit(1);
    }

    InputCommon::Init();

    SDL_SetMainReady();

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);

    std::string window_title = fmt::format("yuzu-tester {} | {}-{}", Common::g_build_fullname,
                                           Common::g_scm_branch, Common::g_scm_desc);
    render_window = SDL_CreateWindow(window_title.c_str(),
                                     SDL_WINDOWPOS_UNDEFINED, // x position
                                     SDL_WINDOWPOS_UNDEFINED, // y position
                                     Layout::ScreenUndocked::Width, Layout::ScreenUndocked::Height,
                                     SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                                         SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);

    if (render_window == nullptr) {
        LOG_CRITICAL(Frontend, "Failed to create SDL2 window! {}", SDL_GetError());
        exit(1);
    }

    gl_context = SDL_GL_CreateContext(render_window);

    if (gl_context == nullptr) {
        LOG_CRITICAL(Frontend, "Failed to create SDL2 GL context! {}", SDL_GetError());
        exit(1);
    }

    if (!gladLoadGLLoader(static_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        LOG_CRITICAL(Frontend, "Failed to initialize GL functions! {}", SDL_GetError());
        exit(1);
    }

    if (!SupportsRequiredGLExtensions()) {
        LOG_CRITICAL(Frontend, "GPU does not support all required OpenGL extensions! Exiting...");
        exit(1);
    }

    SDL_PumpEvents();
    SDL_GL_SetSwapInterval(false);
    LOG_INFO(Frontend, "yuzu-tester Version: {} | {}-{}", Common::g_build_fullname,
             Common::g_scm_branch, Common::g_scm_desc);
    Settings::LogSettings();

    DoneCurrent();
}

EmuWindow_SDL2_Hide::~EmuWindow_SDL2_Hide() {
    InputCommon::Shutdown();
    SDL_GL_DeleteContext(gl_context);
    SDL_Quit();
}

void EmuWindow_SDL2_Hide::SwapBuffers() {
    SDL_GL_SwapWindow(render_window);
}

void EmuWindow_SDL2_Hide::PollEvents() {}

void EmuWindow_SDL2_Hide::MakeCurrent() {
    SDL_GL_MakeCurrent(render_window, gl_context);
}

void EmuWindow_SDL2_Hide::DoneCurrent() {
    SDL_GL_MakeCurrent(render_window, nullptr);
}
