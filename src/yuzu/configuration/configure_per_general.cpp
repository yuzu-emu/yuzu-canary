// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <memory>
#include <utility>

#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QStandardItemModel>
#include <QString>
#include <QTimer>
#include <QTreeView>

#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/xts_archive.h"
#include "core/loader/loader.h"
#include "ui_configure_per_general.h"
#include "yuzu/configuration/config.h"
#include "yuzu/configuration/configure_input.h"
#include "yuzu/configuration/configure_per_general.h"
#include "yuzu/ui_settings.h"
#include "yuzu/util/util.h"

ConfigurePerGameGeneral::ConfigurePerGameGeneral(QWidget* parent, u64 title_id)
    : QDialog(parent), ui(std::make_unique<Ui::ConfigurePerGameGeneral>()), title_id(title_id) {

    ui->setupUi(this);
    setFocusPolicy(Qt::ClickFocus);
    setWindowTitle(tr("Properties"));

    layout = new QVBoxLayout;
    tree_view = new QTreeView;
    item_model = new QStandardItemModel(tree_view);
    tree_view->setModel(item_model);
    tree_view->setAlternatingRowColors(true);
    tree_view->setSelectionMode(QHeaderView::SingleSelection);
    tree_view->setSelectionBehavior(QHeaderView::SelectRows);
    tree_view->setVerticalScrollMode(QHeaderView::ScrollPerPixel);
    tree_view->setHorizontalScrollMode(QHeaderView::ScrollPerPixel);
    tree_view->setSortingEnabled(true);
    tree_view->setEditTriggers(QHeaderView::NoEditTriggers);
    tree_view->setUniformRowHeights(true);
    tree_view->setContextMenuPolicy(Qt::NoContextMenu);

    item_model->insertColumns(0, 2);
    item_model->setHeaderData(0, Qt::Horizontal, "Patch Name");
    item_model->setHeaderData(1, Qt::Horizontal, "Version");

    // We must register all custom types with the Qt Automoc system so that we are able to use it
    // with signals/slots. In this case, QList falls under the umbrells of custom types.
    qRegisterMetaType<QList<QStandardItem*>>("QList<QStandardItem*>");

    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(tree_view);

    ui->scrollArea->setLayout(layout);

    scene = new QGraphicsScene;
    ui->icon_view->setScene(scene);

    connect(item_model, &QStandardItemModel::itemChanged,
            [] { UISettings::values.is_game_list_reload_pending.exchange(true); });

    this->loadConfiguration();
}

ConfigurePerGameGeneral::~ConfigurePerGameGeneral() = default;

void ConfigurePerGameGeneral::applyConfiguration() {
    std::vector<std::string> disabled_addons;

    for (const auto& item : list_items) {
        const auto disabled = item.front()->checkState() == Qt::Unchecked;
        if (disabled)
            disabled_addons.push_back(item.front()->text().toStdString());
    }

    Settings::values.disabled_addons[title_id] = disabled_addons;
}

void ConfigurePerGameGeneral::loadFromFile(FileSys::VirtualFile file) {
    this->file = std::move(file);
    this->loadConfiguration();
}

void ConfigurePerGameGeneral::loadConfiguration() {
    if (file == nullptr)
        return;

    const auto loader = Loader::GetLoader(file);

    ui->display_title_id->setText(fmt::format("{:016X}", title_id).c_str());

    FileSys::PatchManager pm{title_id};
    const auto control = pm.GetControlMetadata();

    if (control.first != nullptr) {
        ui->display_version->setText(QString::fromStdString(control.first->GetVersionString()));
        ui->display_name->setText(QString::fromStdString(control.first->GetApplicationName()));
        ui->display_developer->setText(QString::fromStdString(control.first->GetDeveloperName()));
    } else {
        std::string title;
        if (loader->ReadTitle(title) == Loader::ResultStatus::Success)
            ui->display_name->setText(QString::fromStdString(title));

        std::string developer;
        if (loader->ReadDeveloper(developer) == Loader::ResultStatus::Success)
            ui->display_developer->setText(QString::fromStdString(developer));

        ui->display_version->setText(QStringLiteral("1.0.0"));
    }

    if (control.second != nullptr) {
        scene->clear();

        QPixmap map;
        const auto bytes = control.second->ReadAllBytes();
        map.loadFromData(bytes.data(), bytes.size());

        scene->addPixmap(map.scaled(ui->icon_view->width(), ui->icon_view->height(),
                                    Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    } else {
        std::vector<u8> bytes;
        if (loader->ReadIcon(bytes) == Loader::ResultStatus::Success) {
            scene->clear();

            QPixmap map;
            map.loadFromData(bytes.data(), bytes.size());

            scene->addPixmap(map.scaled(ui->icon_view->width(), ui->icon_view->height(),
                                        Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        }
    }

    FileSys::VirtualFile update_raw;
    loader->ReadUpdateRaw(update_raw);

    const auto& disabled = Settings::values.disabled_addons[title_id];

    for (const auto& patch : pm.GetPatchVersionNames(update_raw)) {
        QStandardItem* first_item = new QStandardItem;
        const auto name = QString::fromStdString(patch.first).replace("[D] ", "");
        first_item->setText(name);
        first_item->setCheckable(true);

        const auto patch_disabled =
            std::find(disabled.begin(), disabled.end(), name.toStdString()) != disabled.end();

        first_item->setCheckState(patch_disabled ? Qt::Unchecked : Qt::Checked);

        list_items.push_back(QList<QStandardItem*>{
            first_item, new QStandardItem{QString::fromStdString(patch.second)}});
        item_model->appendRow(list_items.back());
    }

    tree_view->setColumnWidth(0, 5 * tree_view->width() / 16);

    ui->display_filename->setText(QString::fromStdString(file->GetName()));

    ui->display_format->setText(
        QString::fromStdString(Loader::GetFileTypeString(loader->GetFileType())));

    const auto valueText = ReadableByteSize(file->GetSize());
    ui->display_size->setText(valueText);
}
