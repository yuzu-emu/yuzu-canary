// Copyright 2019 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/service/am/applets/applets.h"

namespace Service::AM::Applets {

enum class PhotoViewerAppletMode : u8 {
    CurrentApp = 0,
    AllApps = 1,
};

class PhotoViewer final : public Applet {
public:
    explicit PhotoViewer(const Core::Frontend::PhotoViewerApplet& frontend);
    ~PhotoViewer() override;

    void Initialize() override;
    bool TransactionComplete() const override;
    ResultCode GetStatus() const override;
    void ExecuteInteractive() override;
    void Execute() override;

    void ViewFinished();

private:
    const Core::Frontend::PhotoViewerApplet& frontend;
    bool complete = false;
    PhotoViewerAppletMode mode = PhotoViewerAppletMode::CurrentApp;
};

class StubApplet final : public Applet {
public:
    explicit StubApplet(AppletId id);
    ~StubApplet() override;

    void Initialize() override;

    bool TransactionComplete() const override;
    ResultCode GetStatus() const override;
    void ExecuteInteractive() override;
    void Execute() override;

private:
    AppletId id;
};

} // namespace Service::AM::Applets
