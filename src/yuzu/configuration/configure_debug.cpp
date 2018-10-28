// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QDesktopServices>
#include <QUrl>
#include "common/file_util.h"
#include "common/logging/backend.h"
#include "common/logging/filter.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/settings.h"
#include "ui_configure_debug.h"
#include "yuzu/configuration/config.h"
#include "yuzu/configuration/configure_debug.h"
#include "yuzu/debugger/console.h"
#include "yuzu/ui_settings.h"

ConfigureDebug::ConfigureDebug(QWidget* parent) : QWidget(parent), ui(new Ui::ConfigureDebug) {
    ui->setupUi(this);
    this->setConfiguration();

    connect(ui->open_log_button, &QPushButton::pressed, []() {
        QString path = QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::LogDir));
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });

    connect(ui->homebrew_args_edit, &QLineEdit::textChanged, [this](const QString& str) {
        if (!ui->program_args_checkbox->isHidden())
            ui->program_args_checkbox->setChecked(true);
    });
}

ConfigureDebug::~ConfigureDebug() = default;

void ConfigureDebug::setPerGame(bool per_game) {
    ui->override_label->setHidden(!per_game);
    ui->program_args_checkbox->setHidden(!per_game);
    ui->groupBox_2->setHidden(per_game);
    ui->groupBox->setHidden(per_game);
}

void ConfigureDebug::loadValuesChange(const PerGameValuesChange& change) {
    ui->program_args_checkbox->setChecked(change.program_args);
}

void ConfigureDebug::mergeValuesChange(PerGameValuesChange& change) {
    change.program_args = ui->program_args_checkbox->isChecked();
}

void ConfigureDebug::setConfiguration() {
    ui->toggle_gdbstub->setChecked(Settings::values.use_gdbstub);
    ui->gdbport_spinbox->setEnabled(Settings::values.use_gdbstub);
    ui->gdbport_spinbox->setValue(Settings::values.gdbstub_port);
    ui->toggle_console->setEnabled(!Core::System::GetInstance().IsPoweredOn());
    ui->toggle_console->setChecked(UISettings::values.show_console);
    ui->log_filter_edit->setText(QString::fromStdString(Settings::values.log_filter));
    ui->homebrew_args_edit->setText(QString::fromStdString(Settings::values->program_args));
}

void ConfigureDebug::applyConfiguration() {
    Settings::values.use_gdbstub = ui->toggle_gdbstub->isChecked();
    Settings::values.gdbstub_port = ui->gdbport_spinbox->value();
    UISettings::values.show_console = ui->toggle_console->isChecked();
    Settings::values.log_filter = ui->log_filter_edit->text().toStdString();
    Settings::values->program_args = ui->homebrew_args_edit->text().toStdString();
    Debugger::ToggleConsole();
    Log::Filter filter;
    filter.ParseFilterString(Settings::values.log_filter);
    Log::SetGlobalFilter(filter);
}
