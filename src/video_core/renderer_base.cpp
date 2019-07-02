// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "core/frontend/emu_window.h"
#include "core/settings.h"
#include "video_core/renderer_base.h"

namespace VideoCore {

RendererBase::RendererBase(Core::Frontend::EmuWindow& window) : render_window{window} {
    RefreshBaseSettings();
}

RendererBase::~RendererBase() = default;

void RendererBase::RefreshBaseSettings() {
    UpdateCurrentFramebufferLayout();

    renderer_settings.use_framelimiter = Settings::values.use_frame_limit;
    renderer_settings.set_background_color = true;
}

void RendererBase::UpdateCurrentFramebufferLayout() {
    const Layout::FramebufferLayout& layout = render_window.GetFramebufferLayout();

    render_window.UpdateCurrentFramebufferLayout(layout.width, layout.height);
}

void RendererBase::RequestScreenshot(void* data, std::function<void()> callback,
                                     const Layout::FramebufferLayout& layout) {
    if (renderer_settings.screenshot_requested) {
        LOG_ERROR(Render, "A screenshot is already requested or in progress, ignoring the request");
        return;
    }
    renderer_settings.screenshot_bits = data;
    renderer_settings.screenshot_complete_callback = std::move(callback);
    renderer_settings.screenshot_framebuffer_layout = layout;
    renderer_settings.screenshot_requested = true;
}

f32 RendererBase::GetCurrentResultantBrightness() const {
    return renderer_settings.current_brightness / 2.0f;
}

void RendererBase::SetBacklightStatus(bool enabled, u64 fade_transition_time) {
    if (fade_transition_time == 0) {
        // Needed to ensure the renderer recognizes that a change must occur.
        fade_transition_time = 1;
    }

    if (enabled && renderer_settings.current_brightness == 0) {
        renderer_settings.current_brightness = current_brightness_backup;
        renderer_settings.backlight_fade_time = fade_transition_time;
    } else if (!enabled && renderer_settings.current_brightness != 0) {
        current_brightness_backup = renderer_settings.current_brightness;
        renderer_settings.current_brightness = 0;
        renderer_settings.backlight_fade_time = fade_transition_time;
    }
}

bool RendererBase::GetBacklightStatus() const {
    return renderer_settings.current_brightness != 0;
}

void RendererBase::SetCurrentBrightness(f32 value) {
    if (value != renderer_settings.current_brightness) {
        renderer_settings.current_brightness = value * 2.0f;
        renderer_settings.backlight_fade_time = 1;
    }
}

} // namespace VideoCore
