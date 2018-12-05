// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <vector>

#include <QKeyEvent>
#include <QList>
#include <QWidget>

#include "core/file_sys/vfs_types.h"

class QTreeView;
class QGraphicsScene;
class QStandardItem;
class QStandardItemModel;

namespace Ui {
class ConfigurePerGameGeneral;
}

class ConfigurePerGameGeneral : public QDialog {
    Q_OBJECT

public:
    explicit ConfigurePerGameGeneral(QWidget* parent, u64 title_id);
    ~ConfigurePerGameGeneral() override;

    /// Save all button configurations to settings file
    void applyConfiguration();

    void loadFromFile(FileSys::VirtualFile file);

private:
    std::unique_ptr<Ui::ConfigurePerGameGeneral> ui;
    FileSys::VirtualFile file;
    u64 title_id;

    QVBoxLayout* layout;
    QTreeView* tree_view;
    QStandardItemModel* item_model;
    QGraphicsScene* scene;

    std::vector<QList<QStandardItem*>> list_items;

    void loadConfiguration();
};
