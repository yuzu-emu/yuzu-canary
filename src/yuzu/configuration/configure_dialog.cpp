// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QHash>
#include <QListWidgetItem>
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

void ConfigureDialog::PopulateSelectionList() {
    const std::array<std::pair<QString, QStringList>, 4> items{
        {{tr("General"), {tr("General"), tr("Web"), tr("Debug"), tr("Game List")}},
         {tr("System"), {tr("System"), tr("Services"), tr("Profiles"), tr("Audio")}},
         {tr("Graphics"), {tr("Graphics")}},
         {tr("Controls"), {tr("Input"), tr("Hotkeys")}}}};

    for (const auto& entry : items) {
        auto* const item = new QListWidgetItem(entry.first);
        item->setData(Qt::UserRole, entry.second);

        ui->selectorList->addItem(item);
    }
}

void ConfigureDialog::UpdateVisibleTabs() {
    const auto items = ui->selectorList->selectedItems();
    if (items.isEmpty())
        return;

    const std::map<QString, QWidget*> widgets = {
        {tr("General"), ui->generalTab},
        {tr("System"), ui->systemTab},
        {tr("Profiles"), ui->profileManagerTab},
        {tr("Input"), ui->inputTab},
        {tr("Hotkeys"), ui->hotkeysTab},
        {tr("Graphics"), ui->graphicsTab},
        {tr("Audio"), ui->audioTab},
        {tr("Debug"), ui->debugTab},
        {tr("Web"), ui->webTab},
        {tr("Game List"), ui->gameListTab},
        {tr("Services"), ui->serviceTab},
    };

    ui->tabWidget->clear();

    const QStringList tabs = items[0]->data(Qt::UserRole).toStringList();

    for (const auto& tab : tabs)
        ui->tabWidget->addTab(widgets.find(tab)->second, tab);
}
