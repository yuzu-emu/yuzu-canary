// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QHash>
#include <QListWidgetItem>
#include "core/settings.h"
#include "ui_configure.h"
#include "yuzu/configuration/config.h"
#include "yuzu/configuration/configure_dialog.h"
#include "yuzu/hotkeys.h"

ConfigureDialog::ConfigureDialog(QWidget* parent, const HotkeyRegistry& registry)
    : QDialog(parent), ui(new Ui::ConfigureDialog) {
    ui->setupUi(this);
    ui->generalTab->PopulateHotkeyList(registry);
    this->setConfiguration();
    this->PopulateSelectionList();
    connect(ui->selectorList, &QListWidget::itemSelectionChanged, this,
            &ConfigureDialog::UpdateVisibleTabs);

    adjustSize();

    ui->selectorList->setCurrentRow(0);
}

ConfigureDialog::~ConfigureDialog() = default;

void ConfigureDialog::setConfiguration() {}

void ConfigureDialog::applyConfiguration() {
    ui->generalTab->applyConfiguration();
    ui->gameListTab->applyConfiguration();
    ui->systemTab->applyConfiguration();
    ui->inputTab->applyConfiguration();
    ui->graphicsTab->applyConfiguration();
    ui->audioTab->applyConfiguration();
    ui->debugTab->applyConfiguration();
    ui->webTab->applyConfiguration();
    Settings::Apply();
}

void ConfigureDialog::PopulateSelectionList() {
    const std::array<std::pair<QString, QStringList>, 4> items{
        {{tr("General"), {tr("General"), tr("Web"), tr("Debug"), tr("Game List")}},
         {tr("System"), {tr("System"), tr("Audio")}},
         {tr("Graphics"), {tr("Graphics")}},
         {tr("Controls"), {tr("Input")}}}};

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
        {tr("General"), ui->generalTab}, {tr("System"), ui->systemTab},
        {tr("Input"), ui->inputTab},     {tr("Graphics"), ui->graphicsTab},
        {tr("Audio"), ui->audioTab},     {tr("Debug"), ui->debugTab},
        {tr("Web"), ui->webTab},         {tr("Game List"), ui->gameListTab}};

    ui->tabWidget->clear();

    const QStringList tabs = items[0]->data(Qt::UserRole).toStringList();

    for (const auto& tab : tabs)
        ui->tabWidget->addTab(widgets.find(tab)->second, tab);
}
