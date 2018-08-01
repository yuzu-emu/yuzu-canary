// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/settings.h"
#include "ui_configure.h"
#include "yuzu/configuration/config.h"
#include "yuzu/configuration/configure_dialog.h"

ConfigureDialog::ConfigureDialog(QWidget* parent) : QDialog(parent), ui(new Ui::ConfigureDialog) {
    ui->setupUi(this);
    this->setConfiguration();
}

ConfigureDialog::~ConfigureDialog() {}

void ConfigureDialog::setConfiguration() {}

void ConfigureDialog::applyConfiguration() {
    ui->generalTab->applyConfiguration();
    ui->systemTab->applyConfiguration();
    ui->inputTab->applyConfiguration();
    ui->graphicsTab->applyConfiguration();
    ui->audioTab->applyConfiguration();
    ui->debugTab->applyConfiguration();
    Settings::Apply();
}
