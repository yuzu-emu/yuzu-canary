// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include "common/common_paths.h"
#include "common/file_util.h"
#include "core/core.h"
#include "core/file_sys/card_image.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/mode.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/submission_package.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/loader.h"
#include "yuzu/compatibility_list.h"
#include "yuzu/game_list.h"
#include "yuzu/game_list_p.h"
#include "yuzu/game_list_worker.h"
#include "yuzu/ui_settings.h"

namespace {

template <typename T>
T GetGameListCachedObject(const std::string& filename, const std::string& ext,
                          const std::function<T()>& generator);

template <>
QString GetGameListCachedObject(const std::string& filename, const std::string& ext,
                                const std::function<QString()>& generator) {
    if (!UISettings::values.cache_game_list || filename == "0000000000000000") {
        return generator();
    }

    const auto path = FileUtil::GetUserPath(FileUtil::UserPath::CacheDir) + DIR_SEP + "game_list" +
                      DIR_SEP + filename + '.' + ext;

    FileUtil::CreateFullPath(path);

    if (!FileUtil::Exists(path)) {
        const auto str = generator();

        std::ofstream stream(path);
        if (stream) {
            stream << str.toStdString();
        }

        return str;
    }

    std::ifstream stream(path);

    if (stream) {
        const std::string out(std::istreambuf_iterator<char>{stream},
                              std::istreambuf_iterator<char>{});
        return QString::fromStdString(out);
    }

    return generator();
}

template <>
std::pair<std::vector<u8>, std::string> GetGameListCachedObject(
    const std::string& filename, const std::string& ext,
    const std::function<std::pair<std::vector<u8>, std::string>()>& generator) {
    if (!UISettings::values.cache_game_list || filename == "0000000000000000") {
        return generator();
    }

    const auto path1 = FileUtil::GetUserPath(FileUtil::UserPath::CacheDir) + DIR_SEP + "game_list" +
                       DIR_SEP + filename + ".jpeg";
    const auto path2 = FileUtil::GetUserPath(FileUtil::UserPath::CacheDir) + DIR_SEP + "game_list" +
                       DIR_SEP + filename + ".appname.txt";

    FileUtil::CreateFullPath(path1);

    if (!FileUtil::Exists(path1) || !FileUtil::Exists(path2)) {
        const auto [icon, nacp] = generator();

        FileUtil::IOFile file1(path1, "wb");
        if (!file1.IsOpen()) {
            LOG_ERROR(Frontend, "Failed to open cache file.");
            return generator();
        }

        if (!file1.Resize(icon.size())) {
            LOG_ERROR(Frontend, "Failed to resize cache file to necessary size.");
            return generator();
        }

        if (file1.WriteBytes(icon.data(), icon.size()) != icon.size()) {
            LOG_ERROR(Frontend, "Failed to write data to cache file.");
            return generator();
        }

        std::ofstream stream2(path2, std::ios::out);
        if (stream2) {
            stream2 << nacp;
        }

        return std::make_pair(icon, nacp);
    }

    FileUtil::IOFile file1(path1, "rb");
    std::ifstream stream2(path2);

    if (!file1.IsOpen()) {
        LOG_ERROR(Frontend, "Failed to open cache file for reading.");
        return generator();
    }

    if (!stream2) {
        LOG_ERROR(Frontend, "Failed to open cache file for reading.");
        return generator();
    }

    std::vector<u8> vec(file1.GetSize());
    file1.ReadBytes(vec.data(), vec.size());

    if (stream2 && !vec.empty()) {
        const std::string out(std::istreambuf_iterator<char>{stream2},
                              std::istreambuf_iterator<char>{});
        return std::make_pair(vec, out);
    }

    return generator();
}

void GetMetadataFromControlNCA(const FileSys::PatchManager& patch_manager, const FileSys::NCA& nca,
                               std::vector<u8>& icon, std::string& name) {
    std::tie(icon, name) = GetGameListCachedObject<std::pair<std::vector<u8>, std::string>>(
        fmt::format("{:016X}", patch_manager.GetTitleID()), {}, [&patch_manager, &nca] {
            const auto [nacp, icon_f] = patch_manager.ParseControlNCA(nca);
            return std::make_pair(icon_f->ReadAllBytes(), nacp->GetApplicationName());
        });
}

bool HasSupportedFileExtension(const std::string& file_name) {
    const QFileInfo file = QFileInfo(QString::fromStdString(file_name));
    return GameList::supported_file_extensions.contains(file.suffix(), Qt::CaseInsensitive);
}

bool IsExtractedNCAMain(const std::string& file_name) {
    return QFileInfo(QString::fromStdString(file_name)).fileName() == QStringLiteral("main");
}

QString FormatGameName(const std::string& physical_name) {
    const QString physical_name_as_qstring = QString::fromStdString(physical_name);
    const QFileInfo file_info(physical_name_as_qstring);

    if (IsExtractedNCAMain(physical_name)) {
        return file_info.dir().path();
    }

    return physical_name_as_qstring;
}

QString FormatPatchNameVersions(const FileSys::PatchManager& patch_manager,
                                Loader::AppLoader& loader, bool updatable = true) {
    QString out;
    FileSys::VirtualFile update_raw;
    loader.ReadUpdateRaw(update_raw);
    for (const auto& kv : patch_manager.GetPatchVersionNames(update_raw)) {
        const bool is_update = kv.first == "Update" || kv.first == "[D] Update";
        if (!updatable && is_update) {
            continue;
        }

        const QString type = QString::fromStdString(kv.first);

        if (kv.second.empty()) {
            out.append(QStringLiteral("%1\n").arg(type));
        } else {
            auto ver = kv.second;

            // Display container name for packed updates
            if (is_update && ver == "PACKED") {
                ver = Loader::GetFileTypeString(loader.GetFileType());
            }

            out.append(QStringLiteral("%1 (%2)\n").arg(type, QString::fromStdString(ver)));
        }
    }

    out.chop(1);
    return out;
}

QList<QStandardItem*> MakeGameListEntry(const std::string& path, const std::string& name,
                                        const std::vector<u8>& icon, Loader::AppLoader& loader,
                                        u64 program_id, const CompatibilityList& compatibility_list,
                                        const FileSys::PatchManager& patch) {
    const auto it = FindMatchingCompatibilityEntry(compatibility_list, program_id);

    // The game list uses this as compatibility number for untested games
    QString compatibility{QStringLiteral("99")};
    if (it != compatibility_list.end()) {
        compatibility = it->second.first;
    }

    const auto file_type = loader.GetFileType();
    const auto file_type_string = QString::fromStdString(Loader::GetFileTypeString(file_type));

    QList<QStandardItem*> list{
        new GameListItemPath(FormatGameName(path), icon, QString::fromStdString(name),
                             file_type_string, program_id),
        new GameListItemCompat(compatibility),
        new GameListItem(file_type_string),
        new GameListItemSize(FileUtil::GetSize(path)),
    };

    if (UISettings::values.show_add_ons) {
        const auto patch_versions = GetGameListCachedObject<QString>(
            fmt::format("{:016X}", patch.GetTitleID()), "pv.txt", [&patch, &loader] {
                return FormatPatchNameVersions(patch, loader, loader.IsRomFSUpdatable());
            });
        list.insert(2, new GameListItem(patch_versions));
    }

    return list;
}
} // Anonymous namespace

GameListWorker::GameListWorker(FileSys::VirtualFilesystem vfs,
                               FileSys::ManualContentProvider* provider, QString dir_path,
                               bool deep_scan, const CompatibilityList& compatibility_list)
    : vfs(std::move(vfs)), provider(provider), dir_path(std::move(dir_path)), deep_scan(deep_scan),
      compatibility_list(compatibility_list) {}

GameListWorker::~GameListWorker() = default;

void GameListWorker::AddTitlesToGameList() {
    const auto& cache = dynamic_cast<FileSys::ContentProviderUnion&>(
        Core::System::GetInstance().GetContentProvider());
    const auto installed_games = cache.ListEntriesFilterOrigin(
        std::nullopt, FileSys::TitleType::Application, FileSys::ContentRecordType::Program);

    for (const auto& [slot, game] : installed_games) {
        if (slot == FileSys::ContentProviderUnionSlot::FrontendManual)
            continue;

        const auto file = cache.GetEntryUnparsed(game.title_id, game.type);
        std::unique_ptr<Loader::AppLoader> loader = Loader::GetLoader(file);
        if (!loader)
            continue;

        std::vector<u8> icon;
        std::string name;
        u64 program_id = 0;
        loader->ReadProgramId(program_id);

        const FileSys::PatchManager patch{program_id};
        const auto control = cache.GetEntry(game.title_id, FileSys::ContentRecordType::Control);
        if (control != nullptr)
            GetMetadataFromControlNCA(patch, *control, icon, name);

        emit EntryReady(MakeGameListEntry(file->GetFullPath(), name, icon, *loader, program_id,
                                          compatibility_list, patch));
    }
}

void GameListWorker::ScanFileSystem(ScanTarget target, const std::string& dir_path,
                                    unsigned int recursion) {
    const auto callback = [this, target, recursion](u64* num_entries_out,
                                                    const std::string& directory,
                                                    const std::string& virtual_name) -> bool {
        if (stop_processing) {
            // Breaks the callback loop.
            return false;
        }

        const std::string physical_name = directory + DIR_SEP + virtual_name;
        const bool is_dir = FileUtil::IsDirectory(physical_name);
        if (!is_dir &&
            (HasSupportedFileExtension(physical_name) || IsExtractedNCAMain(physical_name))) {
            const auto file = vfs->OpenFile(physical_name, FileSys::Mode::Read);
            auto loader = Loader::GetLoader(file);
            if (!loader) {
                return true;
            }

            const auto file_type = loader->GetFileType();
            if ((file_type == Loader::FileType::Unknown || file_type == Loader::FileType::Error) &&
                !UISettings::values.show_unknown) {
                return true;
            }

            u64 program_id = 0;
            const auto res2 = loader->ReadProgramId(program_id);

            if (target == ScanTarget::FillManualContentProvider) {
                if (res2 == Loader::ResultStatus::Success && file_type == Loader::FileType::NCA) {
                    provider->AddEntry(FileSys::TitleType::Application,
                                       FileSys::GetCRTypeFromNCAType(FileSys::NCA{file}.GetType()),
                                       program_id, file);
                } else if (res2 == Loader::ResultStatus::Success &&
                           (file_type == Loader::FileType::XCI ||
                            file_type == Loader::FileType::NSP)) {
                    const auto nsp = file_type == Loader::FileType::NSP
                                         ? std::make_shared<FileSys::NSP>(file)
                                         : FileSys::XCI{file}.GetSecurePartitionNSP();
                    for (const auto& title : nsp->GetNCAs()) {
                        for (const auto& entry : title.second) {
                            provider->AddEntry(entry.first.first, entry.first.second, title.first,
                                               entry.second->GetBaseFile());
                        }
                    }
                }
            } else {
                std::vector<u8> icon;
                const auto res1 = loader->ReadIcon(icon);

                std::string name = " ";
                const auto res3 = loader->ReadTitle(name);

                const FileSys::PatchManager patch{program_id};

                emit EntryReady(MakeGameListEntry(physical_name, name, icon, *loader, program_id,
                                                  compatibility_list, patch));
            }
        } else if (is_dir && recursion > 0) {
            watch_list.append(QString::fromStdString(physical_name));
            ScanFileSystem(target, physical_name, recursion - 1);
        }

        return true;
    };

    FileUtil::ForeachDirectoryEntry(nullptr, dir_path, callback);
}

void GameListWorker::run() {
    stop_processing = false;
    watch_list.append(dir_path);
    provider->ClearAllEntries();
    ScanFileSystem(ScanTarget::FillManualContentProvider, dir_path.toStdString(),
                   deep_scan ? 256 : 0);
    AddTitlesToGameList();
    ScanFileSystem(ScanTarget::PopulateGameList, dir_path.toStdString(), deep_scan ? 256 : 0);
    emit Finished(watch_list);
}

void GameListWorker::Cancel() {
    this->disconnect();
    stop_processing = true;
}
