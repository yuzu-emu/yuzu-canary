// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <memory>
#include <optional>

#include "common/common_types.h"
#include "core/frontend/emu_window.h"
#include "video_core/gpu.h"
#include "video_core/rasterizer_interface.h"

namespace Core::Frontend {
class EmuWindow;
}

namespace VideoCore {

struct RendererSettings {
    std::atomic_bool use_framelimiter{false};
    std::atomic_bool set_background_color{false};

    // Screenshot
    std::atomic<bool> screenshot_requested{false};
    void* screenshot_bits;
    std::function<void()> screenshot_complete_callback;
    Layout::FramebufferLayout screenshot_framebuffer_layout;

    // Backlight & Brightness
    std::atomic<f32> current_brightness{1.f};
    std::atomic<u64> backlight_fade_time{0};
};

class RendererBase : NonCopyable {
public:
    explicit RendererBase(Core::Frontend::EmuWindow& window);
    virtual ~RendererBase();

    /// Swap buffers (render frame)
    virtual void SwapBuffers(const Tegra::FramebufferConfig* framebuffer) = 0;

    /// Initialize the renderer
    virtual bool Init() = 0;

    /// Shutdown the renderer
    virtual void ShutDown() = 0;

    // Getter/setter functions:
    // ------------------------

    f32 GetCurrentFPS() const {
        return m_current_fps;
    }

    int GetCurrentFrame() const {
        return m_current_frame;
    }

    RasterizerInterface& Rasterizer() {
        return *rasterizer;
    }

    const RasterizerInterface& Rasterizer() const {
        return *rasterizer;
    }

    Core::Frontend::EmuWindow& GetRenderWindow() {
        return render_window;
    }

    const Core::Frontend::EmuWindow& GetRenderWindow() const {
        return render_window;
    }

    RendererSettings& Settings() {
        return renderer_settings;
    }

    const RendererSettings& Settings() const {
        return renderer_settings;
    }

    /// Refreshes the settings common to all renderers
    void RefreshBaseSettings();

    /// Request a screenshot of the next frame
    void RequestScreenshot(void* data, std::function<void()> callback,
                           const Layout::FramebufferLayout& layout);

    // Gets the current brightness, even if it has been changed from the set value. Most of the time
    // for yuzu this will simply match what was returned, but implementations are free to change the
    // value in settings.
    f32 GetCurrentResultantBrightness() const;

    void SetBacklightStatus(bool enabled, u64 fade_transition_time);

    bool GetBacklightStatus() const;

    void SetCurrentBrightness(f32 value);

protected:
    Core::Frontend::EmuWindow& render_window; ///< Reference to the render window handle.
    std::unique_ptr<RasterizerInterface> rasterizer;
    f32 m_current_fps = 0.0f; ///< Current framerate, should be set by the renderer
    int m_current_frame = 0;  ///< Current frame, should be set by the renderer

    RendererSettings renderer_settings;

private:
    /// Updates the framebuffer layout of the contained render window handle.
    void UpdateCurrentFramebufferLayout();

    // Value of brightness before backlight switch used to preserve value.
    f32 current_brightness_backup;
};

} // namespace VideoCore
