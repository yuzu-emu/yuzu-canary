// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <cstring>
#include <vector>

#include "common/assert.h"
#include "common/common_funcs.h"
#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/hex_util.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/mode.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/system_archive/system_archive.h"
#include "core/file_sys/vfs_types.h"
#include "core/frontend/applets/general_frontend.h"
#include "core/frontend/applets/web_browser.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/am/applets/web_browser.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/loader.h"

namespace Service::AM::Applets {

enum class WebArgTLVType : u16 {
    InitialURL = 0x1,
    ShopArgumentsURL = 0x2, ///< TODO(DarkLordZach): This is not the official name.
    CallbackURL = 0x3,
    CallbackableURL = 0x4,
    ApplicationID = 0x5,
    DocumentPath = 0x6,
    DocumentKind = 0x7,
    SystemDataID = 0x8,
    ShareStartPage = 0x9,
    Whitelist = 0xA,
    News = 0xB,
    UserID = 0xE,
    AlbumEntry0 = 0xF,
    ScreenShotEnabled = 0x10,
    EcClientCertEnabled = 0x11,
    Unk12 = 0x12,
    PlayReportEnabled = 0x13,
    Unk14 = 0x14,
    Unk15 = 0x15,
    BootDisplayKind = 0x17,
    BackgroundKind = 0x18,
    FooterEnabled = 0x19,
    PointerEnabled = 0x1A,
    LeftStickMode = 0x1B,
    KeyRepeatFrame1 = 0x1C,
    KeyRepeatFrame2 = 0x1D,
    BootAsMediaPlayerInv = 0x1E,
    DisplayUrlKind = 0x1F,
    BootAsMediaPlayer = 0x21,
    ShopJumpEnabled = 0x22,
    MediaAutoPlayEnabled = 0x23,
    LobbyParameter = 0x24,
    ApplicationAlbumEntry = 0x26,
    JsExtensionEnabled = 0x27,
    AdditionalCommentText = 0x28,
    TouchEnabledOnContents = 0x29,
    UserAgentAdditionalString = 0x2A,
    AdditionalMediaData0 = 0x2B,
    MediaPlayerAutoCloseEnabled = 0x2C,
    PageCacheEnabled = 0x2D,
    WebAudioEnabled = 0x2E,
    Unk2F = 0x2F,
    YouTubeVideoWhitelist = 0x31,
    FooterFixedKind = 0x32,
    PageFadeEnabled = 0x33,
    MediaCreatorApplicationRatingAge = 0x34,
    BootLoadingIconEnabled = 0x35,
    PageScrollIndicationEnabled = 0x36,
    MediaPlayerSpeedControlEnabled = 0x37,
    AlbumEntry1 = 0x38,
    AlbumEntry2 = 0x39,
    AlbumEntry3 = 0x3A,
    AdditionalMediaData1 = 0x3B,
    AdditionalMediaData2 = 0x3C,
    AdditionalMediaData3 = 0x3D,
    BootFooterButton = 0x3E,
    OverrideWebAudioVolume = 0x3F,
    OverrideMediaAudioVolume = 0x40,
    BootMode = 0x41,
    WebSessionEnabled = 0x42,
};

enum class ShimKind : u32 {
    Shop = 1,
    Login = 2,
    Offline = 3,
    Share = 4,
    Web = 5,
    Wifi = 6,
    Lobby = 7,
};

enum class ShopWebTarget {
    ApplicationInfo,
    AddOnContentList,
    SubscriptionList,
    ConsumableItemList,
    Home,
    Settings,
};

namespace {

constexpr std::size_t SHIM_KIND_COUNT = 0x8;

struct WebArgHeader {
    u16 count;
    INSERT_PADDING_BYTES(2);
    ShimKind kind;
};
static_assert(sizeof(WebArgHeader) == 0x8, "WebArgHeader has incorrect size.");

struct WebArgTLV {
    WebArgTLVType type;
    u16 size;
    u32 offset;
};
static_assert(sizeof(WebArgTLV) == 0x8, "WebArgTLV has incorrect size.");

struct WebCommonReturnValue {
    u32 result_code;
    INSERT_PADDING_BYTES(0x4);
    std::array<char, 0x1000> last_url;
    u64 last_url_size;
};
static_assert(sizeof(WebCommonReturnValue) == 0x1010, "WebCommonReturnValue has incorrect size.");

struct WebWifiPageArg {
    INSERT_PADDING_BYTES(4);
    std::array<char, 0x100> connection_test_url;
    std::array<char, 0x400> initial_url;
    std::array<u8, 0x10> nifm_network_uuid;
    u32 nifm_requirement;
};
static_assert(sizeof(WebWifiPageArg) == 0x518, "WebWifiPageArg has incorrect size.");

struct WebWifiReturnValue {
    INSERT_PADDING_BYTES(4);
    u32 result;
};
static_assert(sizeof(WebWifiReturnValue) == 0x8, "WebWifiReturnValue has incorrect size.");

enum class OfflineWebSource : u32 {
    OfflineHtmlPage = 0x1,
    ApplicationLegalInformation = 0x2,
    SystemDataPage = 0x3,
};

std::map<WebArgTLVType, std::vector<u8>> GetWebArguments(const std::vector<u8>& arg) {
    if (arg.size() < sizeof(WebArgHeader))
        return {};

    WebArgHeader header{};
    std::memcpy(&header, arg.data(), sizeof(WebArgHeader));

    std::map<WebArgTLVType, std::vector<u8>> out;
    u64 offset = sizeof(WebArgHeader);
    for (std::size_t i = 0; i < header.count; ++i) {
        if (arg.size() < (offset + sizeof(WebArgTLV)))
            return out;

        WebArgTLV tlv{};
        std::memcpy(&tlv, arg.data() + offset, sizeof(WebArgTLV));
        offset += sizeof(WebArgTLV);

        offset += tlv.offset;
        if (arg.size() < (offset + tlv.size))
            return out;

        std::vector<u8> data(tlv.size);
        std::memcpy(data.data(), arg.data() + offset, tlv.size);
        offset += tlv.size;

        out.insert_or_assign(tlv.type, data);
    }

    return out;
}

FileSys::VirtualFile GetApplicationRomFS(const Core::System& system, u64 title_id,
                                         FileSys::ContentRecordType type) {
    const auto& installed{system.GetContentProvider()};
    const auto res = installed.GetEntry(title_id, type);

    if (res != nullptr) {
        return res->GetRomFS();
    }

    if (type == FileSys::ContentRecordType::Data) {
        return FileSys::SystemArchive::SynthesizeSystemArchive(title_id);
    }

    return nullptr;
}

} // Anonymous namespace

WebBrowser::WebBrowser(Core::System& system_, Core::Frontend::WebBrowserApplet& frontend_,
                       Core::Frontend::ECommerceApplet* frontend_e_commerce_)
    : Applet{system_.Kernel()}, frontend(frontend_),
      frontend_e_commerce(frontend_e_commerce_), system{system_} {}

WebBrowser::~WebBrowser() = default;

void WebBrowser::Initialize() {
    Applet::Initialize();

    complete = false;
    temporary_dir.clear();
    filename.clear();
    status = RESULT_SUCCESS;

    const auto web_arg_storage = broker.PopNormalDataToApplet();
    ASSERT(web_arg_storage != nullptr);
    const auto& web_arg = web_arg_storage->GetData();

    ASSERT(web_arg.size() >= 0x8);
    std::memcpy(&kind, web_arg.data() + 0x4, sizeof(ShimKind));

    args = GetWebArguments(web_arg);

    InitializeInternal();
}

bool WebBrowser::TransactionComplete() const {
    return complete;
}

ResultCode WebBrowser::GetStatus() const {
    return status;
}

void WebBrowser::ExecuteInteractive() {
    UNIMPLEMENTED_MSG("Unexpected interactive data recieved!");
}

void WebBrowser::Execute() {
    if (complete) {
        return;
    }

    if (status != RESULT_SUCCESS) {
        complete = true;
        return;
    }

    ExecuteInternal();
}

void WebBrowser::UnpackRomFS() {
    if (unpacked)
        return;

    ASSERT(offline_romfs != nullptr);
    const auto dir =
        FileSys::ExtractRomFS(offline_romfs, FileSys::RomFSExtractionType::SingleDiscard);
    const auto& vfs{system.GetFilesystem()};
    const auto temp_dir = vfs->CreateDirectory(temporary_dir, FileSys::Mode::ReadWrite);
    FileSys::VfsRawCopyD(dir, temp_dir);

    unpacked = true;
}

void WebBrowser::Finalize() {
    complete = true;

    WebCommonReturnValue out{};
    out.result_code = 0;
    out.last_url_size = 0;

    std::vector<u8> data(sizeof(WebCommonReturnValue));
    std::memcpy(data.data(), &out, sizeof(WebCommonReturnValue));

    broker.PushNormalDataFromApplet(IStorage{data});
    broker.SignalStateChanged();

    if (!temporary_dir.empty() && FileUtil::IsDirectory(temporary_dir)) {
        FileUtil::DeleteDirRecursively(temporary_dir);
    }
}

void WebBrowser::InitializeInternal() {
    using WebAppletInitializer = void (WebBrowser::*)();

    constexpr std::array<WebAppletInitializer, SHIM_KIND_COUNT> functions{
        nullptr, &WebBrowser::InitializeShop,
        nullptr, &WebBrowser::InitializeOffline,
        nullptr, nullptr,
        nullptr, nullptr,
    };

    const auto index = static_cast<u32>(kind);

    if (index > functions.size() || functions[index] == nullptr) {
        LOG_ERROR(Service_AM, "Invalid shim_kind={:08X}", index);
        return;
    }

    const auto function = functions[index];
    (this->*function)();
}

void WebBrowser::ExecuteInternal() {
    using WebAppletExecutor = void (WebBrowser::*)();

    constexpr std::array<WebAppletExecutor, SHIM_KIND_COUNT> functions{
        nullptr, &WebBrowser::ExecuteShop,
        nullptr, &WebBrowser::ExecuteOffline,
        nullptr, nullptr,
        nullptr, nullptr,
    };

    const auto index = static_cast<u32>(kind);

    if (index > functions.size() || functions[index] == nullptr) {
        LOG_ERROR(Service_AM, "Invalid shim_kind={:08X}", index);
        return;
    }

    const auto function = functions[index];
    (this->*function)();
}

void WebBrowser::InitializeShop() {
    if (frontend_e_commerce == nullptr) {
        LOG_ERROR(Service_AM, "Missing ECommerce Applet frontend!");
        status = ResultCode(-1);
        return;
    }

    const auto user_id_data = args.find(WebArgTLVType::UserID);

    user_id = std::nullopt;
    if (user_id_data != args.end()) {
        user_id = u128{};
        std::memcpy(user_id->data(), user_id_data->second.data(), sizeof(u128));
    }

    const auto url = args.find(WebArgTLVType::ShopArgumentsURL);

    if (url == args.end()) {
        LOG_ERROR(Service_AM, "Missing EShop Arguments URL for initialization!");
        status = ResultCode(-1);
        return;
    }

    std::vector<std::string> split_query;
    Common::SplitString(Common::StringFromFixedZeroTerminatedBuffer(
                            reinterpret_cast<const char*>(url->second.data()), url->second.size()),
                        '?', split_query);

    // 2 -> Main URL '?' Query Parameters
    // Less is missing info, More is malformed
    if (split_query.size() != 2) {
        LOG_ERROR(Service_AM, "EShop Arguments has more than one question mark, malformed");
        status = ResultCode(-1);
        return;
    }

    std::vector<std::string> queries;
    Common::SplitString(split_query[1], '&', queries);

    const auto split_single_query =
        [](const std::string& in) -> std::pair<std::string, std::string> {
        const auto index = in.find('=');
        if (index == std::string::npos || index == in.size() - 1) {
            return {in, ""};
        }

        return {in.substr(0, index), in.substr(index + 1)};
    };

    std::transform(queries.begin(), queries.end(),
                   std::inserter(shop_query, std::next(shop_query.begin())), split_single_query);

    const auto scene = shop_query.find("scene");

    if (scene == shop_query.end()) {
        LOG_ERROR(Service_AM, "No scene parameter was passed via shop query!");
        status = ResultCode(-1);
        return;
    }

    const std::map<std::string, ShopWebTarget, std::less<>> target_map{
        {"product_detail", ShopWebTarget::ApplicationInfo},
        {"aocs", ShopWebTarget::AddOnContentList},
        {"subscriptions", ShopWebTarget::SubscriptionList},
        {"consumption", ShopWebTarget::ConsumableItemList},
        {"settings", ShopWebTarget::Settings},
        {"top", ShopWebTarget::Home},
    };

    const auto target = target_map.find(scene->second);
    if (target == target_map.end()) {
        LOG_ERROR(Service_AM, "Scene for shop query is invalid! (scene={})", scene->second);
        status = ResultCode(-1);
        return;
    }

    shop_web_target = target->second;

    const auto title_id_data = shop_query.find("dst_app_id");
    if (title_id_data != shop_query.end()) {
        title_id = std::stoull(title_id_data->second, nullptr, 0x10);
    }

    const auto mode_data = shop_query.find("mode");
    if (mode_data != shop_query.end()) {
        shop_full_display = mode_data->second == "full";
    }
}

void WebBrowser::InitializeOffline() {
    if (args.find(WebArgTLVType::DocumentPath) == args.end() ||
        args.find(WebArgTLVType::DocumentKind) == args.end() ||
        args.find(WebArgTLVType::ApplicationID) == args.end()) {
        status = ResultCode(-1);
        LOG_ERROR(Service_AM, "Missing necessary parameters for initialization!");
    }

    const auto url_data = args[WebArgTLVType::DocumentPath];
    filename = Common::StringFromFixedZeroTerminatedBuffer(
        reinterpret_cast<const char*>(url_data.data()), url_data.size());

    OfflineWebSource source;
    ASSERT(args[WebArgTLVType::DocumentKind].size() >= 4);
    std::memcpy(&source, args[WebArgTLVType::DocumentKind].data(), sizeof(OfflineWebSource));

    constexpr std::array<const char*, 3> WEB_SOURCE_NAMES{
        "manual",
        "legal",
        "system",
    };

    temporary_dir =
        FileUtil::SanitizePath(FileUtil::GetUserPath(FileUtil::UserPath::CacheDir) + "web_applet_" +
                                   WEB_SOURCE_NAMES[static_cast<u32>(source) - 1],
                               FileUtil::DirectorySeparator::PlatformDefault);
    FileUtil::DeleteDirRecursively(temporary_dir);

    u64 title_id = 0; // 0 corresponds to current process
    ASSERT(args[WebArgTLVType::ApplicationID].size() >= 0x8);
    std::memcpy(&title_id, args[WebArgTLVType::ApplicationID].data(), sizeof(u64));
    FileSys::ContentRecordType type = FileSys::ContentRecordType::Data;

    switch (source) {
    case OfflineWebSource::OfflineHtmlPage:
        // While there is an AppID TLV field, in official SW this is always ignored.
        title_id = 0;
        type = FileSys::ContentRecordType::HtmlDocument;
        break;
    case OfflineWebSource::ApplicationLegalInformation:
        type = FileSys::ContentRecordType::LegalInformation;
        break;
    case OfflineWebSource::SystemDataPage:
        type = FileSys::ContentRecordType::Data;
        break;
    }

    if (title_id == 0) {
        title_id = system.CurrentProcess()->GetTitleID();
    }

    offline_romfs = GetApplicationRomFS(system, title_id, type);
    if (offline_romfs == nullptr) {
        status = ResultCode(-1);
        LOG_ERROR(Service_AM, "Failed to find offline data for request!");
    }

    std::string path_additional_directory;
    if (source == OfflineWebSource::OfflineHtmlPage) {
        path_additional_directory = std::string(DIR_SEP).append("html-document");
    }

    filename =
        FileUtil::SanitizePath(temporary_dir + path_additional_directory + DIR_SEP + filename,
                               FileUtil::DirectorySeparator::PlatformDefault);
}

void WebBrowser::ExecuteShop() {
    const auto callback = [this]() { Finalize(); };

    const auto check_optional_parameter = [this](const auto& p) {
        if (!p.has_value()) {
            LOG_ERROR(Service_AM, "Missing one or more necessary parameters for execution!");
            status = ResultCode(-1);
            return false;
        }

        return true;
    };

    switch (shop_web_target) {
    case ShopWebTarget::ApplicationInfo:
        if (!check_optional_parameter(title_id))
            return;
        frontend_e_commerce->ShowApplicationInformation(callback, *title_id, user_id,
                                                        shop_full_display, shop_extra_parameter);
        break;
    case ShopWebTarget::AddOnContentList:
        if (!check_optional_parameter(title_id))
            return;
        frontend_e_commerce->ShowAddOnContentList(callback, *title_id, user_id, shop_full_display);
        break;
    case ShopWebTarget::ConsumableItemList:
        if (!check_optional_parameter(title_id))
            return;
        frontend_e_commerce->ShowConsumableItemList(callback, *title_id, user_id);
        break;
    case ShopWebTarget::Home:
        if (!check_optional_parameter(user_id))
            return;
        if (!check_optional_parameter(shop_full_display))
            return;
        frontend_e_commerce->ShowShopHome(callback, *user_id, *shop_full_display);
        break;
    case ShopWebTarget::Settings:
        if (!check_optional_parameter(user_id))
            return;
        if (!check_optional_parameter(shop_full_display))
            return;
        frontend_e_commerce->ShowSettings(callback, *user_id, *shop_full_display);
        break;
    case ShopWebTarget::SubscriptionList:
        if (!check_optional_parameter(title_id))
            return;
        frontend_e_commerce->ShowSubscriptionList(callback, *title_id, user_id);
        break;
    default:
        UNREACHABLE();
    }
}

void WebBrowser::ExecuteOffline() {
    frontend.OpenPageLocal(filename, [this] { UnpackRomFS(); }, [this] { Finalize(); });
}

} // namespace Service::AM::Applets
