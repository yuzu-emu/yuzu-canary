// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/file_sys/vfs.h"
#include "core/settings.h"
#include "ui_configure_per_game.h"
#include "yuzu/configuration/config.h"
#include "yuzu/configuration/configure_per_game_dialog.h"

ConfigurePerGameDialog::ConfigurePerGameDialog(QWidget* parent, Loader::AppLoader& loader,
                                               const PerGameValuesChange& change)
    : QDialog(parent), ui(new Ui::ConfigurePerGameDialog) {
    ui->setupUi(this);
    ui->generalTab->loadGameData(loader);
    ui->generalTab->loadValuesChange(change);
    ui->inputTab->setPerGame(true);
    ui->inputTab->loadValuesChange(change);
    ui->graphicsTab->setPerGame(true);
    ui->graphicsTab->loadValuesChange(change);
    ui->audioTab->setPerGame(true);
    ui->audioTab->loadValuesChange(change);
    ui->debugTab->setPerGame(true);
    ui->debugTab->loadValuesChange(change);
    this->setConfiguration();
}

ConfigurePerGameDialog::~ConfigurePerGameDialog() = default;

void ConfigurePerGameDialog::setConfiguration() {}

PerGameValuesChange ConfigurePerGameDialog::applyConfiguration() {
    PerGameValuesChange out{};
    ui->generalTab->applyConfiguration();
    ui->generalTab->mergeValuesChange(out);
    ui->inputTab->applyConfiguration();
    ui->inputTab->mergeValuesChange(out);
    ui->graphicsTab->applyConfiguration();
    ui->graphicsTab->mergeValuesChange(out);
    ui->audioTab->applyConfiguration();
    ui->audioTab->mergeValuesChange(out);
    ui->debugTab->applyConfiguration();
    ui->debugTab->mergeValuesChange(out);
    Settings::Apply();
    return out;
}
