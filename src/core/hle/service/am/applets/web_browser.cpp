// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/common_paths.h"
#include "common/hex_util.h"
#include "common/logging/backend.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/mode.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/romfs_factory.h"
#include "core/file_sys/vfs_types.h"
#include "core/frontend/applets/web_browser.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/am/applets/web_browser.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/loader.h"

namespace Service::AM::Applets {

// TODO(DarkLordZach): There are other arguments in the WebBuffer structure that are currently not
// parsed, for example footer mode and left stick mode. Some of these are not particularly relevant,
// but some may be worth an implementation.
constexpr u16 WEB_ARGUMENT_URL_TYPE = 0x6;

struct WebBufferHeader {
    u16 count;
    INSERT_PADDING_BYTES(6);
};
static_assert(sizeof(WebBufferHeader) == 0x8, "WebBufferHeader has incorrect size.");

struct WebArgumentHeader {
    u16 type;
    u16 size;
    u32 offset;
};
static_assert(sizeof(WebArgumentHeader) == 0x8, "WebArgumentHeader has incorrect size.");

struct WebArgumentResult {
    u32 result_code;
    std::array<char, 0x1000> last_url;
    u64 last_url_size;
};
static_assert(sizeof(WebArgumentResult) == 0x1010, "WebArgumentResult has incorrect size.");

static std::vector<u8> GetArgumentDataForTagType(const std::vector<u8>& data, u16 type) {
    WebBufferHeader header;
    ASSERT(sizeof(WebBufferHeader) <= data.size());
    std::memcpy(&header, data.data(), sizeof(WebBufferHeader));

    u64 offset = sizeof(WebBufferHeader);
    for (u16 i = 0; i < header.count; ++i) {
        WebArgumentHeader arg;
        ASSERT(offset + sizeof(WebArgumentHeader) <= data.size());
        std::memcpy(&arg, data.data() + offset, sizeof(WebArgumentHeader));
        offset += sizeof(WebArgumentHeader);

        if (arg.type == type) {
            std::vector<u8> out(arg.size);
            offset += arg.offset;
            ASSERT(offset + arg.size <= data.size());
            std::memcpy(out.data(), data.data() + offset, out.size());
            return out;
        }

        offset += arg.offset + arg.size;
    }

    return {};
}

static FileSys::VirtualFile GetManualRomFS() {
    auto& loader{Core::System::GetInstance().GetAppLoader()};

    FileSys::VirtualFile out;
    if (loader.ReadManualRomFS(out) == Loader::ResultStatus::Success)
        return out;

    const auto& installed{FileSystem::GetUnionContents()};
    const auto res = installed.GetEntry(Core::System::GetInstance().CurrentProcess()->GetTitleID(),
                                        FileSys::ContentRecordType::Manual);

    if (res != nullptr)
        return res->GetRomFS();
    return nullptr;
}

WebBrowser::WebBrowser() = default;

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

    const auto url_data = GetArgumentDataForTagType(web_arg, WEB_ARGUMENT_URL_TYPE);
    filename = Common::StringFromFixedZeroTerminatedBuffer(
        reinterpret_cast<const char*>(url_data.data()), url_data.size());

    temporary_dir = FileUtil::SanitizePath(FileUtil::GetUserPath(FileUtil::UserPath::CacheDir) +
                                               "web_applet_manual",
                                           FileUtil::DirectorySeparator::PlatformDefault);
    FileUtil::DeleteDirRecursively(temporary_dir);

    manual_romfs = GetManualRomFS();
    if (manual_romfs == nullptr) {
        status = ResultCode(-1);
        LOG_ERROR(Service_AM, "Failed to find manual for current process!");
    }

    filename =
        FileUtil::SanitizePath(temporary_dir + DIR_SEP + "html-document" + DIR_SEP + filename,
                               FileUtil::DirectorySeparator::PlatformDefault);
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
    if (complete)
        return;

    if (status != RESULT_SUCCESS) {
        complete = true;
        return;
    }

    const auto& frontend{Core::System::GetInstance().GetWebBrowser()};

    frontend.OpenPage(filename, [this] { UnpackRomFS(); }, [this] { Finalize(); });
}

void WebBrowser::UnpackRomFS() {
    if (unpacked)
        return;

    ASSERT(manual_romfs != nullptr);
    const auto dir =
        FileSys::ExtractRomFS(manual_romfs, FileSys::RomFSExtractionType::SingleDiscard);
    const auto& vfs{Core::System::GetInstance().GetFilesystem()};
    const auto temp_dir = vfs->CreateDirectory(temporary_dir, FileSys::Mode::ReadWrite);
    FileSys::VfsRawCopyD(dir, temp_dir);

    unpacked = true;
}

void WebBrowser::Finalize() {
    complete = true;

    WebArgumentResult out{};
    out.result_code = 0;
    out.last_url_size = 0;

    std::vector<u8> data(sizeof(WebArgumentResult));
    std::memcpy(data.data(), &out, sizeof(WebArgumentResult));

    broker.PushNormalDataFromApplet(IStorage{data});
    broker.SignalStateChanged();

    FileUtil::DeleteDirRecursively(temporary_dir);
}

} // namespace Service::AM::Applets
