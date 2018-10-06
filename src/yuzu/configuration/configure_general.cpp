// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/core.h"
#include "core/settings.h"
#include "ui_configure_general.h"
#include "yuzu/configuration/configure_general.h"
#include "yuzu/ui_settings.h"

ConfigureGeneral::ConfigureGeneral(QWidget* parent)
    : QWidget(parent), ui(new Ui::ConfigureGeneral) {

    ui->setupUi(this);

    for (const auto& theme : UISettings::themes) {
        ui->theme_combobox->addItem(theme.first, theme.second);
    }

    this->setConfiguration();

    ui->use_cpu_jit->setEnabled(!Core::System::GetInstance().IsPoweredOn());
    ui->use_docked_mode->setEnabled(!Core::System::GetInstance().IsPoweredOn());
}

ConfigureGeneral::~ConfigureGeneral() = default;

void ConfigureGeneral::setConfiguration() {
    ui->toggle_deepscan->setChecked(UISettings::values.gamedir_deepscan);
    ui->toggle_check_exit->setChecked(UISettings::values.confirm_before_closing);
    ui->theme_combobox->setCurrentIndex(ui->theme_combobox->findData(UISettings::values.theme));
    ui->use_cpu_jit->setChecked(Settings::values.use_cpu_jit);
    ui->use_docked_mode->setChecked(Settings::values.use_docked_mode);
    ui->enable_nfc->setChecked(Settings::values.enable_nfc);
}

void ConfigureGeneral::PopulateHotkeyList(const HotkeyRegistry& registry) {
    ui->widget->Populate(registry);
}

void ConfigureGeneral::applyConfiguration() {
    UISettings::values.gamedir_deepscan = ui->toggle_deepscan->isChecked();
    UISettings::values.confirm_before_closing = ui->toggle_check_exit->isChecked();
    UISettings::values.theme =
        ui->theme_combobox->itemData(ui->theme_combobox->currentIndex()).toString();

    Settings::values.use_cpu_jit = ui->use_cpu_jit->isChecked();
    Settings::values.use_docked_mode = ui->use_docked_mode->isChecked();
    Settings::values.enable_nfc = ui->enable_nfc->isChecked();
}
