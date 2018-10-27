// Copyright 2018 yuzu Emulator Project
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
#include "common/param_package.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/xts_archive.h"
#include "core/loader/loader.h"
#include "input_common/main.h"
#include "yuzu/configuration/config.h"
#include "yuzu/configuration/configure_input.h"
#include "yuzu/configuration/configure_per_general.h"
#include "yuzu/ui_settings.h"
#include "yuzu/util/util.h"

ConfigurePerGameGeneral::ConfigurePerGameGeneral(QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::ConfigurePerGameGeneral>()) {
    ui->setupUi(this);
    setFocusPolicy(Qt::ClickFocus);

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
    item_model->setHeaderData(0, Qt::Horizontal, tr("Patch Name"));
    item_model->setHeaderData(1, Qt::Horizontal, tr("Version"));

    // We must register all custom types with the Qt Automoc system so that we are able to use it
    // with signals/slots. In this case, QList falls under the umbrells of custom types.
    qRegisterMetaType<QList<QStandardItem*>>("QList<QStandardItem*>");

    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(tree_view);

    ui->scrollArea->setLayout(layout);

    scene = new QGraphicsScene;
    ui->icon_view->setScene(scene);

    connect(item_model, &QStandardItemModel::itemChanged, this,
            []() { UISettings::values.is_game_list_reload_pending.exchange(true); });
}

ConfigurePerGameGeneral::~ConfigurePerGameGeneral() = default;

void ConfigurePerGameGeneral::applyConfiguration() {
    std::vector<std::string> disabled_add_ons;

    for (const auto& item : list_items) {
        const auto disabled = item.front()->checkState() == Qt::Unchecked;
        if (disabled)
            disabled_add_ons.push_back(item.front()->text().toStdString());
    }

    Settings::values->disabled_patches = disabled_add_ons;

    Settings::values->use_docked_mode = ui->use_docked_mode->checkState() == Qt::PartiallyChecked
                                            ? Settings::values.default_game.use_docked_mode
                                            : ui->use_docked_mode->isChecked();
}

void ConfigurePerGameGeneral::loadGameData(Loader::AppLoader& loader) {
    u64 program_id{};
    if (loader.ReadProgramId(program_id) == Loader::ResultStatus::Success) {
        ui->display_title_id->setText(QStringLiteral("%1").arg(program_id, 16, 16, QChar{'0'}));

        FileSys::PatchManager pm{program_id};
        const auto control = pm.GetControlMetadata();

        if (control.first != nullptr) {
            ui->display_version->setText(QString::fromStdString(control.first->GetVersionString()));
            ui->display_name->setText(QString::fromStdString(control.first->GetApplicationName()));
            ui->display_developer->setText(
                QString::fromStdString(control.first->GetDeveloperName()));
        } else {
            std::string title;
            if (loader.ReadTitle(title) == Loader::ResultStatus::Success)
                ui->display_name->setText(QString::fromStdString(title));

            std::string developer;
            if (loader.ReadDeveloper(developer) == Loader::ResultStatus::Success)
                ui->display_developer->setText(QString::fromStdString(developer));

            ui->display_version->setText("1.0.0");
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
            if (loader.ReadIcon(bytes) == Loader::ResultStatus::Success) {
                scene->clear();

                QPixmap map;
                map.loadFromData(bytes.data(), bytes.size());

                scene->addPixmap(map.scaled(ui->icon_view->width(), ui->icon_view->height(),
                                            Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
            }
        }

        FileSys::VirtualFile update_raw;
        loader.ReadUpdateRaw(update_raw);

        const auto& disabled = Settings::values[program_id].disabled_patches;

        for (const auto& patch : pm.GetPatchVersionNames(update_raw)) {
            QStandardItem* first_item = new QStandardItem;
            first_item->setText(QString::fromStdString(patch.first));
            first_item->setCheckable(true);

            const auto patch_disabled =
                std::find(disabled.begin(), disabled.end(), patch.first) != disabled.end();

            first_item->setCheckState(patch_disabled ? Qt::Unchecked : Qt::Checked);

            list_items.push_back(QList<QStandardItem*>{
                first_item, new QStandardItem{QString::fromStdString(patch.second)}});
            item_model->appendRow(list_items.back());
        }

        tree_view->setColumnWidth(0, 5 * tree_view->width() / 16);
    }

    const auto file = loader.GetFile();

    ui->display_filename->setText(QString::fromStdString(file->GetName()));

    ui->display_format->setText(
        QString::fromStdString(Loader::GetFileTypeString(loader.GetFileType())));

    QString valueText = ReadableByteSize(file->GetSize());
    ui->display_size->setText(valueText);
}

void ConfigurePerGameGeneral::loadValuesChange(const PerGameValuesChange& change) {
    ui->use_docked_mode->setCheckState(
        change.use_docked_mode ? (Settings::values->use_docked_mode ? Qt::Checked : Qt::Unchecked)
                               : Qt::PartiallyChecked);
}

void ConfigurePerGameGeneral::mergeValuesChange(PerGameValuesChange& change) {
    change.use_docked_mode = ui->use_docked_mode->checkState() != Qt::PartiallyChecked;
}
