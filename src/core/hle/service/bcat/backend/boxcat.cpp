// Copyright 2019 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <fmt/ostream.h>
#include <httplib.h>
#include <json.hpp>
#include <mbedtls/sha256.h>
#include "common/hex_util.h"
#include "common/logging/backend.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/file_sys/vfs.h"
#include "core/file_sys/vfs_libzip.h"
#include "core/file_sys/vfs_vector.h"
#include "core/frontend/applets/error.h"
#include "core/hle/service/am/applets/applets.h"
#include "core/hle/service/bcat/backend/boxcat.h"
#include "core/settings.h"

namespace {

// Prevents conflicts with windows macro called CreateFile
FileSys::VirtualFile VfsCreateFileWrap(FileSys::VirtualDir dir, std::string_view name) {
    return dir->CreateFile(name);
}

// Prevents conflicts with windows macro called DeleteFile
bool VfsDeleteFileWrap(FileSys::VirtualDir dir, std::string_view name) {
    return dir->DeleteFile(name);
}

} // Anonymous namespace

namespace Service::BCAT {

constexpr ResultCode ERROR_GENERAL_BCAT_FAILURE{ErrorModule::BCAT, 1};

constexpr char BOXCAT_HOSTNAME[] = "api.yuzu-emu.org";

// Formatted using fmt with arg[0] = hex title id
constexpr char BOXCAT_PATHNAME_DATA[] = "/boxcat/titles/{:016X}/data";
constexpr char BOXCAT_PATHNAME_LAUNCHPARAM[] = "/boxcat/titles/{:016X}/launchparam";

constexpr char BOXCAT_PATHNAME_EVENTS[] = "/boxcat/events";

constexpr char BOXCAT_API_VERSION[] = "1";
constexpr char BOXCAT_CLIENT_TYPE[] = "yuzu";

// HTTP status codes for Boxcat
enum class ResponseStatus {
    Ok = 200,               ///< Operation completed successfully.
    BadClientVersion = 301, ///< The Boxcat-Client-Version doesn't match the server.
    NoUpdate = 304,         ///< The digest provided would match the new data, no need to update.
    NoMatchTitleId = 404,   ///< The title ID provided doesn't have a boxcat implementation.
    NoMatchBuildId = 406,   ///< The build ID provided is blacklisted (potentially because of format
                            ///< issues or whatnot) and has no data.
};

enum class DownloadResult {
    Success = 0,
    NoResponse,
    GeneralWebError,
    NoMatchTitleId,
    NoMatchBuildId,
    InvalidContentType,
    GeneralFSError,
    BadClientVersion,
};

constexpr std::array<const char*, 8> DOWNLOAD_RESULT_LOG_MESSAGES{
    "Success",
    "There was no response from the server.",
    "There was a general web error code returned from the server.",
    "The title ID of the current game doesn't have a boxcat implementation. If you believe an "
    "implementation should be added, contact yuzu support.",
    "The build ID of the current version of the game is marked as incompatible with the current "
    "BCAT distribution. Try upgrading or downgrading your game version or contacting yuzu support.",
    "The content type of the web response was invalid.",
    "There was a general filesystem error while saving the zip file.",
    "The server is either too new or too old to serve the request. Try using the latest version of "
    "an official release of yuzu.",
};

std::ostream& operator<<(std::ostream& os, DownloadResult result) {
    return os << DOWNLOAD_RESULT_LOG_MESSAGES.at(static_cast<std::size_t>(result));
}

constexpr u32 PORT = 443;
constexpr u32 TIMEOUT_SECONDS = 30;
constexpr u64 VFS_COPY_BLOCK_SIZE = 1ull << 24; // 4MB

namespace {

std::string GetBINFilePath(u64 title_id) {
    return fmt::format("{}bcat/{:016X}/launchparam.bin",
                       FileUtil::GetUserPath(FileUtil::UserPath::CacheDir), title_id);
}

std::string GetZIPFilePath(u64 title_id) {
    return fmt::format("{}bcat/{:016X}/data.zip",
                       FileUtil::GetUserPath(FileUtil::UserPath::CacheDir), title_id);
}

// If the error is something the user should know about (build ID mismatch, bad client version),
// display an error.
void HandleDownloadDisplayResult(DownloadResult res) {
    if (res == DownloadResult::Success || res == DownloadResult::NoResponse ||
        res == DownloadResult::GeneralWebError || res == DownloadResult::GeneralFSError ||
        res == DownloadResult::NoMatchTitleId || res == DownloadResult::InvalidContentType) {
        return;
    }

    const auto& frontend{Core::System::GetInstance().GetAppletManager().GetAppletFrontendSet()};
    frontend.error->ShowCustomErrorText(
        ResultCode(-1), "There was an error while attempting to use Boxcat.",
        DOWNLOAD_RESULT_LOG_MESSAGES[static_cast<std::size_t>(res)], [] {});
}

bool VfsRawCopyProgress(FileSys::VirtualFile src, FileSys::VirtualFile dest,
                        std::string_view dir_name, ProgressServiceBackend& progress,
                        std::size_t block_size = 0x1000) {
    if (src == nullptr || dest == nullptr || !src->IsReadable() || !dest->IsWritable())
        return false;
    if (!dest->Resize(src->GetSize()))
        return false;

    progress.StartDownloadingFile(dir_name, src->GetName(), src->GetSize());

    std::vector<u8> temp(std::min(block_size, src->GetSize()));
    for (std::size_t i = 0; i < src->GetSize(); i += block_size) {
        const auto read = std::min(block_size, src->GetSize() - i);

        if (src->Read(temp.data(), read, i) != read) {
            return false;
        }

        if (dest->Write(temp.data(), read, i) != read) {
            return false;
        }

        progress.UpdateFileProgress(i);
    }

    progress.FinishDownloadingFile();

    return true;
}

bool VfsRawCopyDProgressSingle(FileSys::VirtualDir src, FileSys::VirtualDir dest,
                               ProgressServiceBackend& progress, std::size_t block_size = 0x1000) {
    if (src == nullptr || dest == nullptr || !src->IsReadable() || !dest->IsWritable())
        return false;

    for (const auto& file : src->GetFiles()) {
        const auto out_file = VfsCreateFileWrap(dest, file->GetName());
        if (!VfsRawCopyProgress(file, out_file, src->GetName(), progress, block_size)) {
            return false;
        }
    }
    progress.CommitDirectory(src->GetName());

    return true;
}

bool VfsRawCopyDProgress(FileSys::VirtualDir src, FileSys::VirtualDir dest,
                         ProgressServiceBackend& progress, std::size_t block_size = 0x1000) {
    if (src == nullptr || dest == nullptr || !src->IsReadable() || !dest->IsWritable())
        return false;

    for (const auto& dir : src->GetSubdirectories()) {
        const auto out = dest->CreateSubdirectory(dir->GetName());
        if (!VfsRawCopyDProgressSingle(dir, out, progress, block_size)) {
            return false;
        }
    }

    return true;
}

} // Anonymous namespace

class Boxcat::Client {
public:
    Client(std::string path, u64 title_id, u64 build_id)
        : path(std::move(path)), title_id(title_id), build_id(build_id) {}

    DownloadResult DownloadDataZip() {
        return DownloadInternal(fmt::format(BOXCAT_PATHNAME_DATA, title_id), TIMEOUT_SECONDS,
                                "application/zip");
    }

    DownloadResult DownloadLaunchParam() {
        return DownloadInternal(fmt::format(BOXCAT_PATHNAME_LAUNCHPARAM, title_id),
                                TIMEOUT_SECONDS / 3, "application/octet-stream");
    }

private:
    DownloadResult DownloadInternal(const std::string& resolved_path, u32 timeout_seconds,
                                    const std::string& content_type_name) {
        if (client == nullptr) {
            client = std::make_unique<httplib::SSLClient>(BOXCAT_HOSTNAME, PORT, timeout_seconds);
        }

        httplib::Headers headers{
            {std::string("Boxcat-Client-Version"), std::string(BOXCAT_API_VERSION)},
            {std::string("Boxcat-Client-Type"), std::string(BOXCAT_CLIENT_TYPE)},
            {std::string("Boxcat-Build-Id"), fmt::format("{:016X}", build_id)},
        };

        if (FileUtil::Exists(path)) {
            FileUtil::IOFile file{path, "rb"};
            if (file.IsOpen()) {
                std::vector<u8> bytes(file.GetSize());
                file.ReadBytes(bytes.data(), bytes.size());
                const auto digest = DigestFile(bytes);
                headers.insert(
                    {std::string("If-None-Match"), Common::HexArrayToString(digest, false)});
            }
        }

        const auto response = client->Get(resolved_path.c_str(), headers);
        if (response == nullptr)
            return DownloadResult::NoResponse;

        if (response->status == static_cast<int>(ResponseStatus::NoUpdate))
            return DownloadResult::Success;
        if (response->status == static_cast<int>(ResponseStatus::BadClientVersion))
            return DownloadResult::BadClientVersion;
        if (response->status == static_cast<int>(ResponseStatus::NoMatchTitleId))
            return DownloadResult::NoMatchTitleId;
        if (response->status == static_cast<int>(ResponseStatus::NoMatchBuildId))
            return DownloadResult::NoMatchBuildId;
        if (response->status != static_cast<int>(ResponseStatus::Ok))
            return DownloadResult::GeneralWebError;

        const auto content_type = response->headers.find("content-type");
        if (content_type == response->headers.end() ||
            content_type->second.find(content_type_name) == std::string::npos) {
            return DownloadResult::InvalidContentType;
        }

        FileUtil::CreateFullPath(path);
        FileUtil::IOFile file{path, "wb"};
        if (!file.IsOpen())
            return DownloadResult::GeneralFSError;
        if (!file.Resize(response->body.size()))
            return DownloadResult::GeneralFSError;
        if (file.WriteBytes(response->body.data(), response->body.size()) != response->body.size())
            return DownloadResult::GeneralFSError;

        return DownloadResult::Success;
    }

    using Digest = std::array<u8, 0x20>;
    static Digest DigestFile(std::vector<u8> bytes) {
        Digest out{};
        mbedtls_sha256(bytes.data(), bytes.size(), out.data(), 0);
        return out;
    }

    std::unique_ptr<httplib::Client> client;
    std::string path;
    u64 title_id;
    u64 build_id;
};

Boxcat::Boxcat(DirectoryGetter getter) : Backend(std::move(getter)) {}

Boxcat::~Boxcat() = default;

void SynchronizeInternal(DirectoryGetter dir_getter, TitleIDVersion title,
                         ProgressServiceBackend& progress,
                         std::optional<std::string> dir_name = {}) {
    progress.SetNeedHLELock(true);

    if (Settings::values.bcat_boxcat_local) {
        LOG_INFO(Service_BCAT, "Boxcat using local data by override, skipping download.");
        const auto dir = dir_getter(title.title_id);
        if (dir)
            progress.SetTotalSize(dir->GetSize());
        progress.FinishDownload(RESULT_SUCCESS);
        return;
    }

    const auto zip_path{GetZIPFilePath(title.title_id)};
    Boxcat::Client client{zip_path, title.title_id, title.build_id};

    progress.StartConnecting();

    const auto res = client.DownloadDataZip();
    if (res != DownloadResult::Success) {
        LOG_ERROR(Service_BCAT, "Boxcat synchronization failed with error '{}'!", res);

        if (res == DownloadResult::NoMatchBuildId || res == DownloadResult::NoMatchTitleId) {
            FileUtil::Delete(zip_path);
        }

        HandleDownloadDisplayResult(res);
        progress.FinishDownload(ERROR_GENERAL_BCAT_FAILURE);
        return;
    }

    progress.StartProcessingDataList();

    FileUtil::IOFile zip{zip_path, "rb"};
    const auto size = zip.GetSize();
    std::vector<u8> bytes(size);
    if (!zip.IsOpen() || size == 0 || zip.ReadBytes(bytes.data(), bytes.size()) != bytes.size()) {
        LOG_ERROR(Service_BCAT, "Boxcat failed to read ZIP file at path '{}'!", zip_path);
        progress.FinishDownload(ERROR_GENERAL_BCAT_FAILURE);
        return;
    }

    const auto extracted = FileSys::ExtractZIP(std::make_shared<FileSys::VectorVfsFile>(bytes));
    if (extracted == nullptr) {
        LOG_ERROR(Service_BCAT, "Boxcat failed to extract ZIP file!");
        progress.FinishDownload(ERROR_GENERAL_BCAT_FAILURE);
        return;
    }

    if (dir_name == std::nullopt) {
        progress.SetTotalSize(extracted->GetSize());

        const auto target_dir = dir_getter(title.title_id);
        if (target_dir == nullptr || !VfsRawCopyDProgress(extracted, target_dir, progress)) {
            LOG_ERROR(Service_BCAT, "Boxcat failed to copy extracted ZIP to target directory!");
            progress.FinishDownload(ERROR_GENERAL_BCAT_FAILURE);
            return;
        }
    } else {
        const auto target_dir = dir_getter(title.title_id);
        if (target_dir == nullptr) {
            LOG_ERROR(Service_BCAT, "Boxcat failed to get directory for title ID!");
            progress.FinishDownload(ERROR_GENERAL_BCAT_FAILURE);
            return;
        }

        const auto target_sub = target_dir->GetSubdirectory(*dir_name);
        const auto source_sub = extracted->GetSubdirectory(*dir_name);

        progress.SetTotalSize(source_sub->GetSize());

        std::vector<std::string> filenames;
        {
            const auto files = target_sub->GetFiles();
            std::transform(files.begin(), files.end(), std::back_inserter(filenames),
                           [](const auto& vfile) { return vfile->GetName(); });
        }

        for (const auto& filename : filenames) {
            VfsDeleteFileWrap(target_sub, filename);
        }

        if (target_sub == nullptr || source_sub == nullptr ||
            !VfsRawCopyDProgressSingle(source_sub, target_sub, progress)) {
            LOG_ERROR(Service_BCAT, "Boxcat failed to copy extracted ZIP to target directory!");
            progress.FinishDownload(ERROR_GENERAL_BCAT_FAILURE);
            return;
        }
    }

    progress.FinishDownload(RESULT_SUCCESS);
}

bool Boxcat::Synchronize(TitleIDVersion title, ProgressServiceBackend& progress) {
    is_syncing.exchange(true);
    std::thread([this, title, &progress] { SynchronizeInternal(dir_getter, title, progress); })
        .detach();
    return true;
}

bool Boxcat::SynchronizeDirectory(TitleIDVersion title, std::string name,
                                  ProgressServiceBackend& progress) {
    is_syncing.exchange(true);
    std::thread(
        [this, title, name, &progress] { SynchronizeInternal(dir_getter, title, progress, name); })
        .detach();
    return true;
}

bool Boxcat::Clear(u64 title_id) {
    if (Settings::values.bcat_boxcat_local) {
        LOG_INFO(Service_BCAT, "Boxcat using local data by override, skipping clear.");
        return true;
    }

    const auto dir = dir_getter(title_id);

    std::vector<std::string> dirnames;

    for (const auto& subdir : dir->GetSubdirectories())
        dirnames.push_back(subdir->GetName());

    for (const auto& subdir : dirnames) {
        if (!dir->DeleteSubdirectoryRecursive(subdir))
            return false;
    }

    return true;
}

void Boxcat::SetPassphrase(u64 title_id, const Passphrase& passphrase) {
    LOG_DEBUG(Service_BCAT, "called, title_id={:016X}, passphrase={}", title_id,
              Common::HexArrayToString(passphrase));
}

std::optional<std::vector<u8>> Boxcat::GetLaunchParameter(TitleIDVersion title) {
    const auto path{GetBINFilePath(title.title_id)};

    if (Settings::values.bcat_boxcat_local) {
        LOG_INFO(Service_BCAT, "Boxcat using local data by override, skipping download.");
    } else {
        Boxcat::Client client{path, title.title_id, title.build_id};

        const auto res = client.DownloadLaunchParam();
        if (res != DownloadResult::Success) {
            LOG_ERROR(Service_BCAT, "Boxcat synchronization failed with error '{}'!", res);

            if (res == DownloadResult::NoMatchBuildId || res == DownloadResult::NoMatchTitleId) {
                FileUtil::Delete(path);
            }

            HandleDownloadDisplayResult(res);
            return std::nullopt;
        }
    }

    FileUtil::IOFile bin{path, "rb"};
    const auto size = bin.GetSize();
    std::vector<u8> bytes(size);
    if (!bin.IsOpen() || size == 0 || bin.ReadBytes(bytes.data(), bytes.size()) != bytes.size()) {
        LOG_ERROR(Service_BCAT, "Boxcat failed to read launch parameter binary at path '{}'!",
                  path);
        return std::nullopt;
    }

    return bytes;
}

Boxcat::StatusResult Boxcat::GetStatus(std::optional<std::string>& global,
                                       std::map<std::string, EventStatus>& games) {
    httplib::SSLClient client{BOXCAT_HOSTNAME, static_cast<int>(PORT),
                              static_cast<int>(TIMEOUT_SECONDS)};

    httplib::Headers headers{
        {std::string("Boxcat-Client-Version"), std::string(BOXCAT_API_VERSION)},
        {std::string("Boxcat-Client-Type"), std::string(BOXCAT_CLIENT_TYPE)},
    };

    const auto response = client.Get(BOXCAT_PATHNAME_EVENTS, headers);
    if (response == nullptr)
        return StatusResult::Offline;

    if (response->status == static_cast<int>(ResponseStatus::BadClientVersion))
        return StatusResult::BadClientVersion;

    try {
        nlohmann::json json = nlohmann::json::parse(response->body);

        if (!json["online"].get<bool>())
            return StatusResult::Offline;

        if (json["global"].is_null())
            global = std::nullopt;
        else
            global = json["global"].get<std::string>();

        if (json["games"].is_array()) {
            for (const auto object : json["games"]) {
                if (object.is_object() && object.find("name") != object.end()) {
                    EventStatus detail{};
                    if (object["header"].is_string()) {
                        detail.header = object["header"].get<std::string>();
                    } else {
                        detail.header = std::nullopt;
                    }

                    if (object["footer"].is_string()) {
                        detail.footer = object["footer"].get<std::string>();
                    } else {
                        detail.footer = std::nullopt;
                    }

                    if (object["events"].is_array()) {
                        for (const auto& event : object["events"]) {
                            if (!event.is_string())
                                continue;
                            detail.events.push_back(event.get<std::string>());
                        }
                    }

                    games.insert_or_assign(object["name"], std::move(detail));
                }
            }
        }

        return StatusResult::Success;
    } catch (const nlohmann::json::parse_error& e) {
        return StatusResult::ParseError;
    }
}

} // namespace Service::BCAT
