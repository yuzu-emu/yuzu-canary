// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <string_view>

namespace Core::Frontend {

class WebBrowserApplet {
public:
    virtual ~WebBrowserApplet();

    virtual void OpenPage(std::string_view url, std::function<void()> unpack_romfs_callback,
                          std::function<void()> finished_callback) const = 0;
};

class DefaultWebBrowserApplet final : public WebBrowserApplet {
public:
    ~DefaultWebBrowserApplet() override;

    void OpenPage(std::string_view url, std::function<void()> unpack_romfs_callback,
                  std::function<void()> finished_callback) const override;
};

} // namespace Core::Frontend
