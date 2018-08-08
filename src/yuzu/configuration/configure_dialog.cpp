// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

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
    Settings::Apply();
}
