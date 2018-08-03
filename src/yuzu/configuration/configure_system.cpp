// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QMessageBox>
#include "core/core.h"
#include "core/settings.h"
#include "ui_configure_system.h"
#include "yuzu/configuration/configure_system.h"
#include "yuzu/main.h"

static const std::array<int, 12> days_in_month = {{
    31,
    29,
    31,
    30,
    31,
    30,
    31,
    31,
    30,
    31,
    30,
    31,
}};

ConfigureSystem::ConfigureSystem(QWidget* parent) : QWidget(parent), ui(new Ui::ConfigureSystem) {
    ui->setupUi(this);
    connect(ui->combo_birthmonth,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            &ConfigureSystem::updateBirthdayComboBox);
    connect(ui->button_regenerate_console_id, &QPushButton::clicked, this,
            &ConfigureSystem::refreshConsoleID);

    this->setConfiguration();
}

ConfigureSystem::~ConfigureSystem() {}

void ConfigureSystem::setConfiguration() {
    enabled = !Core::System::GetInstance().IsPoweredOn();
    ui->edit_username->setText(QString::fromStdString(Settings::values.username));
    ui->combo_language->setCurrentIndex(Settings::values.language_index);
}

void ConfigureSystem::ReadSystemSettings() {}

void ConfigureSystem::applyConfiguration() {
    if (!enabled)
        return;
    Settings::values.username = ui->edit_username->text().toStdString();
    Settings::values.language_index = ui->combo_language->currentIndex();
    Settings::Apply();
}

void ConfigureSystem::updateBirthdayComboBox(int birthmonth_index) {
    if (birthmonth_index < 0 || birthmonth_index >= 12)
        return;

    // store current day selection
    int birthday_index = ui->combo_birthday->currentIndex();

    // get number of days in the new selected month
    int days = days_in_month[birthmonth_index];

    // if the selected day is out of range,
    // reset it to 1st
    if (birthday_index < 0 || birthday_index >= days)
        birthday_index = 0;

    // update the day combo box
    ui->combo_birthday->clear();
    for (int i = 1; i <= days; ++i) {
        ui->combo_birthday->addItem(QString::number(i));
    }

    // restore the day selection
    ui->combo_birthday->setCurrentIndex(birthday_index);
}

void ConfigureSystem::refreshConsoleID() {
    QMessageBox::StandardButton reply;
    QString warning_text = tr("This will replace your current virtual Switch with a new one. "
                              "Your current virtual Switch will not be recoverable. "
                              "This might have unexpected effects in games. This might fail, "
                              "if you use an outdated config savegame. Continue?");
    reply = QMessageBox::critical(this, tr("Warning"), warning_text,
                                  QMessageBox::No | QMessageBox::Yes);
    if (reply == QMessageBox::No)
        return;
    u64 console_id{};
    ui->label_console_id->setText(
        tr("Console ID: 0x%1").arg(QString::number(console_id, 16).toUpper()));
}
