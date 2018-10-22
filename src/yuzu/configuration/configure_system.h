// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>

#include <QList>
#include <QWidget>
#include "core/hle/service/acc/profile_manager.h"

class QGraphicsScene;
class QStandardItem;
class QStandardItemModel;
class QTreeView;
class QVBoxLayout;

namespace Ui {
class ConfigureSystem;
}

class ConfigureSystem : public QWidget {
    Q_OBJECT

public:
    explicit ConfigureSystem(QWidget* parent = nullptr);
    ~ConfigureSystem();

    void applyConfiguration();
    void setConfiguration();

    void PopulateUserList();
    void UpdateCurrentUser();

public slots:
    void updateBirthdayComboBox(int birthmonth_index);
    void refreshConsoleID();

    void SelectUser(const QModelIndex& index);
    void AddUser();
    void RenameUser();
    void DeleteUser();
    void SetUserImage();

private:
    void ReadSystemSettings();
    std::string GetAccountUsername(Service::Account::UUID uuid) const;

    QVBoxLayout* layout;
    QTreeView* tree_view;
    QStandardItemModel* item_model;
    QGraphicsScene* scene;

    std::vector<QList<QStandardItem*>> list_items;

    std::unique_ptr<Ui::ConfigureSystem> ui;
    bool enabled;

    int birthmonth, birthday;
    int language_index;
    int sound_index;

    std::unique_ptr<Service::Account::ProfileManager> profile_manager;
};
