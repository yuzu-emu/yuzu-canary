// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/file_sys/vfs_types.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/am/applets/applets.h"

namespace Service::AM::Applets {

class WebBrowser final : public Applet {
public:
    WebBrowser();
    ~WebBrowser() override;

    void Initialize() override;

    bool TransactionComplete() const override;
    ResultCode GetStatus() const override;
    void ExecuteInteractive() override;
    void Execute() override;

    // Callback to be fired when the frontend needs the manual RomFS unpacked to temporary
    // directory. This is a blocking call and may take a while as some manuals can be up to 100MB in
    // size. Attempting to access files at filename before invocation is likely to not work.
    void UnpackRomFS();

    // Callback to be fired when the frontend is finished browsing. This will delete the temporary
    // manual RomFS extracted files, so ensure this is only called at actual finalization.
    void Finalize();

private:
    bool complete = false;
    bool unpacked = false;
    ResultCode status = RESULT_SUCCESS;

    FileSys::VirtualFile manual_romfs;
    std::string temporary_dir;
    std::string filename;
};

} // namespace Service::AM::Applets
