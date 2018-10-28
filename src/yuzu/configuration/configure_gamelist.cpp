// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <utility>

#include "common/common_types.h"
#include "core/settings.h"
#include "ui_configure_gamelist.h"
#include "yuzu/configuration/configure_gamelist.h"
#include "yuzu/ui_settings.h"

namespace {
constexpr std::array<std::pair<u32, const char*>, 5> default_icon_sizes{{
    std::make_pair(0, QT_TR_NOOP("None")),
    std::make_pair(32, QT_TR_NOOP("Small (32x32)")),
    std::make_pair(64, QT_TR_NOOP("Standard (64x64)")),
    std::make_pair(128, QT_TR_NOOP("Large (128x128)")),
    std::make_pair(256, QT_TR_NOOP("Full Size (256x256)")),
}};

constexpr std::array<const char*, 4> row_text_names{{
    QT_TR_NOOP("Filename"),
    QT_TR_NOOP("Filetype"),
    QT_TR_NOOP("Title ID"),
    QT_TR_NOOP("Title Name"),
}};
} // Anonymous namespace

ConfigureGameList::ConfigureGameList(QWidget* parent)
    : QWidget(parent), ui(new Ui::ConfigureGameList) {
    ui->setupUi(this);

    InitializeIconSizeComboBox();
    InitializeRowComboBoxes();

    this->setConfiguration();

    // Force game list reload if any of the relevant settings are changed.
    connect(ui->show_unknown, &QCheckBox::stateChanged, this,
            &ConfigureGameList::RequestGameListUpdate);
    connect(ui->icon_size_combobox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ConfigureGameList::RequestGameListUpdate);
    connect(ui->row_1_text_combobox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ConfigureGameList::RequestGameListUpdate);
    connect(ui->row_2_text_combobox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ConfigureGameList::RequestGameListUpdate);
}

ConfigureGameList::~ConfigureGameList() = default;

void ConfigureGameList::applyConfiguration() {
    UISettings::values.show_unknown = ui->show_unknown->isChecked();
    UISettings::values.icon_size = ui->icon_size_combobox->currentData().toUInt();
    UISettings::values.row_1_text_id = ui->row_1_text_combobox->currentData().toUInt();
    UISettings::values.row_2_text_id = ui->row_2_text_combobox->currentData().toUInt();
    Settings::Apply();
}

void ConfigureGameList::RequestGameListUpdate() {
    UISettings::values.is_game_list_reload_pending.exchange(true);
}

void ConfigureGameList::setConfiguration() {
    ui->show_unknown->setChecked(UISettings::values.show_unknown);
    ui->icon_size_combobox->setCurrentIndex(
        ui->icon_size_combobox->findData(UISettings::values.icon_size));
    ui->row_1_text_combobox->setCurrentIndex(
        ui->row_1_text_combobox->findData(UISettings::values.row_1_text_id));
    ui->row_2_text_combobox->setCurrentIndex(
        ui->row_2_text_combobox->findData(UISettings::values.row_2_text_id));
}

void ConfigureGameList::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
        return;
    }

    QWidget::changeEvent(event);
}

void ConfigureGameList::RetranslateUI() {
    ui->retranslateUi(this);

    for (int i = 0; i < ui->icon_size_combobox->count(); i++) {
        ui->icon_size_combobox->setItemText(i, tr(default_icon_sizes[i].second));
    }

    for (int i = 0; i < ui->row_1_text_combobox->count(); i++) {
        const QString name = tr(row_text_names[i]);

        ui->row_1_text_combobox->setItemText(i, name);
        ui->row_2_text_combobox->setItemText(i, name);
    }
}

void ConfigureGameList::InitializeIconSizeComboBox() {
    for (const auto& size : default_icon_sizes) {
        ui->icon_size_combobox->addItem(size.second, size.first);
    }
}

void ConfigureGameList::InitializeRowComboBoxes() {
    for (std::size_t i = 0; i < row_text_names.size(); ++i) {
        ui->row_1_text_combobox->addItem(row_text_names[i], QVariant::fromValue(i));
        ui->row_2_text_combobox->addItem(row_text_names[i], QVariant::fromValue(i));
    }
}
