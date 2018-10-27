// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <QKeyEvent>
#include <QList>
#include <QWidget>
#include "core/file_sys/vfs.h"
#include "ui_configure_per_general.h"
#include "yuzu/configuration/config.h"

class QGraphicsScene;
class QStandardItem;
class QStandardItemModel;
class QTreeView;

namespace Loader {
class AppLoader;
}

namespace Ui {
class ConfigurePerGameGeneral;
}

class ConfigurePerGameGeneral : public QWidget {
    Q_OBJECT

public:
    explicit ConfigurePerGameGeneral(QWidget* parent = nullptr);
    ~ConfigurePerGameGeneral() override;

    /// Save all button configurations to settings file
    void applyConfiguration();

    void loadGameData(Loader::AppLoader& file);

    void loadValuesChange(const PerGameValuesChange& change);
    void mergeValuesChange(PerGameValuesChange& change);

private:
    std::unique_ptr<Ui::ConfigurePerGameGeneral> ui;

    QVBoxLayout* layout;
    QTreeView* tree_view;
    QStandardItemModel* item_model;
    QGraphicsScene* scene;

    std::vector<QList<QStandardItem*>> list_items;
};
