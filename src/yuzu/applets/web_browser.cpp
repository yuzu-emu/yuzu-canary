// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <mutex>

#include <QKeyEvent>

#include "core/hle/lock.h"
#include "yuzu/applets/web_browser.h"
#include "yuzu/main.h"

#ifdef YUZU_USE_QT_WEB_ENGINE

constexpr char NX_SHIM_INJECT_SCRIPT[] = R"(
    window.nx = {};
    window.nx.playReport = {};
    window.nx.playReport.setCounterSetIdentifier = function () {
        console.log("nx.playReport.setCounterSetIdentifier called - unimplemented");
    };

    window.nx.playReport.incrementCounter = function () {
        console.log("nx.playReport.incrementCounter called - unimplemented");
    };

    window.nx.footer = {};
    window.nx.footer.unsetAssign = function () {
        console.log("nx.footer.unsetAssign called - unimplemented");
    };

    var yuzu_key_callbacks = [];
    window.nx.footer.setAssign = function(key, discard1, func, discard2) {
        switch (key) {
        case 'A':
            yuzu_key_callbacks[0] = func;
            break;
        case 'B':
            yuzu_key_callbacks[1] = func;
            break;
        case 'X':
            yuzu_key_callbacks[2] = func;
            break;
        case 'Y':
            yuzu_key_callbacks[3] = func;
            break;
        case 'L':
            yuzu_key_callbacks[6] = func;
            break;
        case 'R':
            yuzu_key_callbacks[7] = func;
            break;
        }
    };

    var applet_done = false;
    window.nx.endApplet = function() {
        applet_done = true;
    };
)";

QString GetNXShimInjectionScript() {
    return QString::fromStdString(NX_SHIM_INJECT_SCRIPT);
}

NXInputWebEngineView::NXInputWebEngineView(QWidget* parent) : QWebEngineView(parent) {}

void NXInputWebEngineView::keyPressEvent(QKeyEvent* event) {
    parent()->event(event);
}

void NXInputWebEngineView::keyReleaseEvent(QKeyEvent* event) {
    parent()->event(event);
}

#endif

QtWebBrowser::QtWebBrowser(GMainWindow& main_window) {
    connect(this, &QtWebBrowser::MainWindowOpenPage, &main_window, &GMainWindow::WebBrowserOpenPage,
            Qt::QueuedConnection);
    connect(&main_window, &GMainWindow::WebBrowserUnpackRomFS, this,
            &QtWebBrowser::MainWindowUnpackRomFS, Qt::QueuedConnection);
    connect(&main_window, &GMainWindow::WebBrowserFinishedBrowsing, this,
            &QtWebBrowser::MainWindowFinishedBrowsing, Qt::QueuedConnection);
}

QtWebBrowser::~QtWebBrowser() = default;

void QtWebBrowser::OpenPage(std::string_view url, std::function<void()> unpack_romfs_callback,
                            std::function<void()> finished_callback) const {
    this->unpack_romfs_callback = unpack_romfs_callback;
    this->finished_callback = finished_callback;

    const auto index = url.find('?');
    if (index == std::string::npos) {
        emit MainWindowOpenPage(url, "");
    } else {
        const auto front = url.substr(0, index);
        const auto back = url.substr(index);
        emit MainWindowOpenPage(front, back);
    }
}

void QtWebBrowser::MainWindowUnpackRomFS() {
    // Acquire the HLE mutex
    std::lock_guard<std::recursive_mutex> lock(HLE::g_hle_lock);
    unpack_romfs_callback();
}

void QtWebBrowser::MainWindowFinishedBrowsing() {
    // Acquire the HLE mutex
    std::lock_guard<std::recursive_mutex> lock(HLE::g_hle_lock);
    finished_callback();
}
