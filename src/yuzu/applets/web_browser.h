// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <QObject>

#ifdef YUZU_USE_QT_WEB_ENGINE
#include <QWebEngineView>
#endif

#include "core/frontend/applets/web_browser.h"

class GMainWindow;

#ifdef YUZU_USE_QT_WEB_ENGINE

QString GetNXShimInjectionScript();

class NXInputWebEngineView : public QWebEngineView {
public:
    explicit NXInputWebEngineView(QWidget* parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
};

#endif

class QtWebBrowser final : public QObject, public Core::Frontend::WebBrowserApplet {
    Q_OBJECT

public:
    explicit QtWebBrowser(GMainWindow& main_window);
    ~QtWebBrowser() override;

    void OpenPage(std::string_view url, std::function<void()> unpack_romfs_callback,
                  std::function<void()> finished_callback) const override;

signals:
    void MainWindowOpenPage(std::string_view filename, std::string_view additional_args) const;

public slots:
    void MainWindowUnpackRomFS();
    void MainWindowFinishedBrowsing();

private:
    mutable std::function<void()> unpack_romfs_callback;
    mutable std::function<void()> finished_callback;
};
