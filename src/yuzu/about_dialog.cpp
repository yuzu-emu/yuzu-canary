// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QIcon>
#include "common/scm_rev.h"
#include "ui_aboutdialog.h"
#include "yuzu/about_dialog.h"

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent), ui(new Ui::AboutDialog) {
    ui->setupUi(this);
    ui->labelLogo->setPixmap(QIcon::fromTheme("yuzu").pixmap(200));
    ui->labelBuildInfo->setText(
        ui->labelBuildInfo->text().arg(Common::g_build_fullname, Common::g_scm_branch,
                                       Common::g_scm_desc, QString(Common::g_build_date).left(10)));
}

AboutDialog::~AboutDialog() = default;
