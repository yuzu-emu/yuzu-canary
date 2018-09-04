// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include <QCoreApplication>
#include <QImage>
#include <QObject>
#include <QRunnable>
#include <QStandardItem>
#include <QString>

#include "common/common_types.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "yuzu/ui_settings.h"
#include "yuzu/util/util.h"

namespace FileSys {
class NCA;
class RegisteredCache;
class VfsFilesystem;
} // namespace FileSys

/**
 * Gets the default icon (for games without valid SMDH)
 * @param large If true, returns large icon (48x48), otherwise returns small icon (24x24)
 * @return QPixmap default icon
 */
static QPixmap GetDefaultIcon(u32 size) {
    QPixmap icon(size, size);
    icon.fill(Qt::transparent);
    return icon;
}

static auto FindMatchingCompatibilityEntry(
    const std::unordered_map<std::string, std::pair<QString, QString>>& compatibility_list,
    u64 program_id) {
    return std::find_if(
        compatibility_list.begin(), compatibility_list.end(),
        [program_id](const std::pair<std::string, std::pair<QString, QString>>& element) {
            std::string pid = fmt::format("{:016X}", program_id);
            return element.first == pid;
        });
}

class GameListItem : public QStandardItem {

public:
    GameListItem() = default;
    explicit GameListItem(const QString& string) : QStandardItem(string) {}
};

/**
 * A specialization of GameListItem for path values.
 * This class ensures that for every full path value it holds, a correct string representation
 * of just the filename (with no extension) will be displayed to the user.
 * If this class receives valid SMDH data, it will also display game icons and titles.
 */
class GameListItemPath : public GameListItem {
public:
    static const int FullPathRole = Qt::UserRole + 1;
    static const int TitleRole = Qt::UserRole + 2;
    static const int ProgramIdRole = Qt::UserRole + 3;
    static const int FileTypeRole = Qt::UserRole + 4;

    GameListItemPath() = default;
    GameListItemPath(const QString& game_path, const std::vector<u8>& picture_data,
                     const QString& game_name, const QString& game_type, u64 program_id) {
        setData(game_path, FullPathRole);
        setData(game_name, TitleRole);
        setData(qulonglong(program_id), ProgramIdRole);
        setData(game_type, FileTypeRole);

        const u32 size = UISettings::values.icon_size;

        QPixmap picture;
        if (!picture.loadFromData(picture_data.data(), static_cast<u32>(picture_data.size()))) {
            picture = GetDefaultIcon(size);
        }
        picture = picture.scaled(size, size);

        setData(picture, Qt::DecorationRole);
    }

    QVariant data(int role) const override {
        if (role == Qt::DisplayRole) {
            std::string filename;
            Common::SplitPath(data(FullPathRole).toString().toStdString(), nullptr, &filename,
                              nullptr);

            const std::array<QString, 4> row_data{{
                QString::fromStdString(filename),
                data(FileTypeRole).toString(),
                QString::fromStdString(fmt::format("0x{:016X}", data(ProgramIdRole).toULongLong())),
                data(TitleRole).toString(),
            }};

            const auto& row1 = row_data.at(UISettings::values.row_1_text_id);
            const auto& row2 = row_data.at(UISettings::values.row_2_text_id);

            if (row1.isEmpty() || row1 == row2)
                return row2;
            if (row2.isEmpty())
                return row1;

            return row1 + "\n    " + row2;
        }

        return GameListItem::data(role);
    }
};

class GameListItemCompat : public GameListItem {
    Q_DECLARE_TR_FUNCTIONS(GameListItemCompat)
public:
    static const int CompatNumberRole = Qt::UserRole + 1;
    GameListItemCompat() = default;
    explicit GameListItemCompat(const QString& compatiblity) {
        struct CompatStatus {
            QString color;
            const char* text;
            const char* tooltip;
        };
        // clang-format off
        static const std::map<QString, CompatStatus> status_data = {
        {"0",  {"#5c93ed", QT_TR_NOOP("Perfect"),    QT_TR_NOOP("Game functions flawless with no audio or graphical glitches, all tested functionality works as intended without\nany workarounds needed.")}},
        {"1",  {"#47d35c", QT_TR_NOOP("Great"),      QT_TR_NOOP("Game functions with minor graphical or audio glitches and is playable from start to finish. May require some\nworkarounds.")}},
        {"2",  {"#94b242", QT_TR_NOOP("Okay"),       QT_TR_NOOP("Game functions with major graphical or audio glitches, but game is playable from start to finish with\nworkarounds.")}},
        {"3",  {"#f2d624", QT_TR_NOOP("Bad"),        QT_TR_NOOP("Game functions, but with major graphical or audio glitches. Unable to progress in specific areas due to glitches\neven with workarounds.")}},
        {"4",  {"#FF0000", QT_TR_NOOP("Intro/Menu"), QT_TR_NOOP("Game is completely unplayable due to major graphical or audio glitches. Unable to progress past the Start\nScreen.")}},
        {"5",  {"#828282", QT_TR_NOOP("Won't Boot"), QT_TR_NOOP("The game crashes when attempting to startup.")}},
        {"99", {"#000000", QT_TR_NOOP("Not Tested"), QT_TR_NOOP("The game has not yet been tested.")}}};
        // clang-format on

        auto iterator = status_data.find(compatiblity);
        if (iterator == status_data.end()) {
            LOG_WARNING(Frontend, "Invalid compatibility number {}", compatiblity.toStdString());
            return;
        }
        CompatStatus status = iterator->second;
        setData(compatiblity, CompatNumberRole);
        setText(QObject::tr(status.text));
        setToolTip(QObject::tr(status.tooltip));
        setData(CreateCirclePixmapFromColor(status.color), Qt::DecorationRole);
    }

    bool operator<(const QStandardItem& other) const override {
        return data(CompatNumberRole) < other.data(CompatNumberRole);
    }
};

/**
 * A specialization of GameListItem for size values.
 * This class ensures that for every numerical size value it holds (in bytes), a correct
 * human-readable string representation will be displayed to the user.
 */
class GameListItemSize : public GameListItem {

public:
    static const int SizeRole = Qt::UserRole + 1;

    GameListItemSize() = default;
    explicit GameListItemSize(const qulonglong size_bytes) {
        setData(size_bytes, SizeRole);
    }

    void setData(const QVariant& value, int role) override {
        // By specializing setData for SizeRole, we can ensure that the numerical and string
        // representations of the data are always accurate and in the correct format.
        if (role == SizeRole) {
            qulonglong size_bytes = value.toULongLong();
            GameListItem::setData(ReadableByteSize(size_bytes), Qt::DisplayRole);
            GameListItem::setData(value, SizeRole);
        } else {
            GameListItem::setData(value, role);
        }
    }

    /**
     * This operator is, in practice, only used by the TreeView sorting systems.
     * Override it so that it will correctly sort by numerical value instead of by string
     * representation.
     */
    bool operator<(const QStandardItem& other) const override {
        return data(SizeRole).toULongLong() < other.data(SizeRole).toULongLong();
    }
};

/**
 * Asynchronous worker object for populating the game list.
 * Communicates with other threads through Qt's signal/slot system.
 */
class GameListWorker : public QObject, public QRunnable {
    Q_OBJECT

public:
    GameListWorker(
        std::shared_ptr<FileSys::VfsFilesystem> vfs, QString dir_path, bool deep_scan,
        const std::unordered_map<std::string, std::pair<QString, QString>>& compatibility_list);
    ~GameListWorker() override;

public slots:
    /// Starts the processing of directory tree information.
    void run() override;
    /// Tells the worker that it should no longer continue processing. Thread-safe.
    void Cancel();

signals:
    /**
     * The `EntryReady` signal is emitted once an entry has been prepared and is ready
     * to be added to the game list.
     * @param entry_items a list with `QStandardItem`s that make up the columns of the new entry.
     */
    void EntryReady(QList<QStandardItem*> entry_items);

    /**
     * After the worker has traversed the game directory looking for entries, this signal is emmited
     * with a list of folders that should be watched for changes as well.
     */
    void Finished(QStringList watch_list);

private:
    std::shared_ptr<FileSys::VfsFilesystem> vfs;
    std::map<u64, std::shared_ptr<FileSys::NCA>> nca_control_map;
    QStringList watch_list;
    QString dir_path;
    bool deep_scan;
    const std::unordered_map<std::string, std::pair<QString, QString>>& compatibility_list;
    std::atomic_bool stop_processing;

    void AddInstalledTitlesToGameList();
    void FillControlMap(const std::string& dir_path);
    void AddFstEntriesToGameList(const std::string& dir_path, unsigned int recursion = 0);
};
