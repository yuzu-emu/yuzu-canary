// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QModelIndex>
#include <QSettings>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QString>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include "common/common_types.h"
#include "uisettings.h"
#include "yuzu/compatibility_list.h"

class GameListWorker;
class GameListSearchField;
class GameListDir;
class GMainWindow;

namespace FileSys {
class ManualContentProvider;
class VfsFilesystem;
} // namespace FileSys

enum class GameListOpenTarget {
    SaveData,
    ModData,
};

class GameList : public QWidget {
    Q_OBJECT

public:
    enum {
        COLUMN_NAME,
        COLUMN_COMPATIBILITY,
        COLUMN_ADD_ONS,
        COLUMN_FILE_TYPE,
        COLUMN_SIZE,
        COLUMN_COUNT, // Number of columns
    };

    explicit GameList(std::shared_ptr<FileSys::VfsFilesystem> vfs,
                      FileSys::ManualContentProvider* provider, GMainWindow* parent = nullptr);
    ~GameList() override;

    QString getLastFilterResultItem() const;
    void clearFilter();
    void setFilterFocus();
    void setFilterVisible(bool visibility);
    bool isEmpty() const;

    void LoadCompatibilityList();
    void PopulateAsync(QVector<UISettings::GameDir>& game_dirs);

    void SaveInterfaceLayout();
    void LoadInterfaceLayout();

    static const QStringList supported_file_extensions;

signals:
    void GameChosen(QString game_path);
    void ShouldCancelWorker();
    void OpenFolderRequested(u64 program_id, GameListOpenTarget target);
    void OpenTransferableShaderCacheRequested(u64 program_id);
    void OpenResolutionProfileRequested(u64 program_id);
    void DumpRomFSRequested(u64 program_id, const std::string& game_path);
    void CopyTIDRequested(u64 program_id);
    void NavigateToGamedbEntryRequested(u64 program_id,
                                        const CompatibilityList& compatibility_list);
    void OpenPerGameGeneralRequested(const std::string& file);
    void OpenDirectory(const QString& directory);
    void AddDirectory();
    void ShowList(bool show);

private slots:
    void onItemExpanded(const QModelIndex& item);
    void onTextChanged(const QString& new_text);
    void onFilterCloseClicked();
    void onUpdateThemedIcons();

private:
    void AddDirEntry(GameListDir* entry_items);
    void AddEntry(const QList<QStandardItem*>& entry_items, GameListDir* parent);
    void ValidateEntry(const QModelIndex& item);
    void DonePopulating(QStringList watch_list);

    void RefreshGameDirectory();

    void PopupContextMenu(const QPoint& menu_location);
    void AddGamePopup(QMenu& context_menu, u64 program_id, std::string path);
    void AddCustomDirPopup(QMenu& context_menu, QModelIndex selected);
    void AddPermDirPopup(QMenu& context_menu, QModelIndex selected);

    std::shared_ptr<FileSys::VfsFilesystem> vfs;
    FileSys::ManualContentProvider* provider;
    GameListSearchField* search_field;
    GMainWindow* main_window = nullptr;
    QVBoxLayout* layout = nullptr;
    QTreeView* tree_view = nullptr;
    QStandardItemModel* item_model = nullptr;
    GameListWorker* current_worker = nullptr;
    QFileSystemWatcher* watcher = nullptr;
    CompatibilityList compatibility_list;

    friend class GameListSearchField;
};

Q_DECLARE_METATYPE(GameListOpenTarget);

class GameListPlaceholder : public QWidget {
    Q_OBJECT
public:
    explicit GameListPlaceholder(GMainWindow* parent = nullptr);
    ~GameListPlaceholder();

signals:
    void AddDirectory();

private slots:
    void onUpdateThemedIcons();

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QVBoxLayout* layout = nullptr;
    QLabel* image = nullptr;
    QLabel* text = nullptr;
};
