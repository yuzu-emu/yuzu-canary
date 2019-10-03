// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QHash>
#include <QListWidgetItem>
#include <QSignalBlocker>
#include "core/settings.h"
#include "ui_configure.h"
#include "yuzu/configuration/config.h"
#include "yuzu/configuration/configure_dialog.h"
#include "yuzu/configuration/configure_input_player.h"
#include "yuzu/hotkeys.h"

ConfigureDialog::ConfigureDialog(QWidget* parent, HotkeyRegistry& registry)
    : QDialog(parent), ui(new Ui::ConfigureDialog), registry(registry) {
    ui->setupUi(this);
    ui->hotkeysTab->Populate(registry);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    SetConfiguration();
    PopulateSelectionList();

    connect(ui->selectorList, &QListWidget::itemSelectionChanged, this,
            &ConfigureDialog::UpdateVisibleTabs);

    adjustSize();
    ui->selectorList->setCurrentRow(0);
}

ConfigureDialog::~ConfigureDialog() = default;

void ConfigureDialog::SetConfiguration() {}

void ConfigureDialog::ApplyConfiguration() {
    ui->generalTab->ApplyConfiguration();
    ui->gameListTab->ApplyConfiguration();
    ui->systemTab->ApplyConfiguration();
    ui->profileManagerTab->ApplyConfiguration();
    ui->filesystemTab->applyConfiguration();
    ui->inputTab->ApplyConfiguration();
    ui->hotkeysTab->ApplyConfiguration(registry);
    ui->graphicsTab->ApplyConfiguration();
    ui->audioTab->ApplyConfiguration();
    ui->debugTab->ApplyConfiguration();
    ui->webTab->ApplyConfiguration();
    ui->serviceTab->ApplyConfiguration();
    Settings::Apply();
    Settings::LogSettings();
}

void ConfigureDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QDialog::changeEvent(event);
}

void ConfigureDialog::RetranslateUI() {
    const int old_row = ui->selectorList->currentRow();
    const int old_index = ui->tabWidget->currentIndex();

    ui->retranslateUi(this);

    PopulateSelectionList();
    ui->selectorList->setCurrentRow(old_row);

    UpdateVisibleTabs();
    ui->tabWidget->setCurrentIndex(old_index);
}

Q_DECLARE_METATYPE(QList<QWidget*>);

void ConfigureDialog::PopulateSelectionList() {
    const std::array<std::pair<QString, QList<QWidget*>>, 4> items{
        {{tr("General"), {ui->generalTab, ui->webTab, ui->debugTab, ui->gameListTab}},
         {tr("System"),
          {ui->systemTab, ui->profileManagerTab, ui->serviceTab, ui->filesystemTab, ui->audioTab}},
         {tr("Graphics"), {ui->graphicsTab}},
         {tr("Controls"), {ui->inputTab, ui->hotkeysTab}}},
    };

    [[maybe_unused]] const QSignalBlocker blocker(ui->selectorList);

    ui->selectorList->clear();
    for (const auto& entry : items) {
        auto* const item = new QListWidgetItem(entry.first);
        item->setData(Qt::UserRole, QVariant::fromValue(entry.second));

        ui->selectorList->addItem(item);
    }
}

void ConfigureDialog::UpdateVisibleTabs() {
    const auto items = ui->selectorList->selectedItems();
    if (items.isEmpty()) {
        return;
    }

    const std::map<QWidget*, QString> widgets = {
        {ui->generalTab, tr("General")},
        {ui->systemTab, tr("System")},
        {ui->profileManagerTab, tr("Profiles")},
        {ui->inputTab, tr("Input")},
        {ui->hotkeysTab, tr("Hotkeys")},
        {ui->graphicsTab, tr("Graphics")},
        {ui->audioTab, tr("Audio")},
        {ui->debugTab, tr("Debug")},
        {ui->webTab, tr("Web")},
        {ui->gameListTab, tr("Game List")},
        {ui->filesystemTab, tr("Filesystem")},
        {ui->serviceTab, tr("Services")},
    };

    [[maybe_unused]] const QSignalBlocker blocker(ui->tabWidget);

    ui->tabWidget->clear();

    const QList<QWidget*> tabs = qvariant_cast<QList<QWidget*>>(items[0]->data(Qt::UserRole));

    for (const auto tab : tabs) {
        ui->tabWidget->addTab(tab, widgets.at(tab));
    }
}
