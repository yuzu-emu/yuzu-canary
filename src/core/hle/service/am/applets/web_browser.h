// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <map>
#include "core/file_sys/vfs_types.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/am/applets/applets.h"

namespace Service::AM::Applets {

enum class ShimKind : u32;
enum class ShopWebTarget;
enum class WebArgTLVType : u16;

class WebBrowser final : public Applet {
public:
    WebBrowser(Core::Frontend::WebBrowserApplet& frontend, u64 current_process_title_id,
               Core::Frontend::ECommerceApplet* frontend_e_commerce = nullptr);

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
    void InitializeInternal();
    void ExecuteInternal();

    // Specific initializers for the types of web applets
    void InitializeShop();
    void InitializeOffline();

    // Specific executors for the types of web applets
    void ExecuteShop();
    void ExecuteOffline();

    Core::Frontend::WebBrowserApplet& frontend;

    // Extra frontends for specialized functions
    Core::Frontend::ECommerceApplet* frontend_e_commerce;

    bool complete = false;
    bool unpacked = false;
    ResultCode status = RESULT_SUCCESS;

    u64 current_process_title_id;

    ShimKind kind;
    std::map<WebArgTLVType, std::vector<u8>> args;

    FileSys::VirtualFile offline_romfs;
    std::string temporary_dir;
    std::string filename;

    ShopWebTarget shop_web_target;
    std::map<std::string, std::string, std::less<>> shop_query;
    std::optional<u64> title_id = 0;
    std::optional<u128> user_id;
    std::optional<bool> shop_full_display;
    std::string shop_extra_parameter;
};

} // namespace Service::AM::Applets
