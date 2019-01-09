// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "core/frontend/applets/web_browser.h"

namespace Core::Frontend {

WebBrowserApplet::~WebBrowserApplet() = default;

DefaultWebBrowserApplet::~DefaultWebBrowserApplet() = default;

void DefaultWebBrowserApplet::OpenPage(std::string_view filename,
                                       std::function<void()> unpack_romfs_callback,
                                       std::function<void()> finished_callback) const {
    LOG_INFO(Service_AM,
             "(STUBBED) called - No suitable web browser implementation found to open website page "
             "at '{}'!",
             filename);
    finished_callback();
}

} // namespace Core::Frontend
